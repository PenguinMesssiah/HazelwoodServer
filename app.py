from flask import Flask, Blueprint, request, jsonify, send_from_directory, abort
from pymongo.errors import ServerSelectionTimeoutError, ConnectionFailure
from flask_cors import CORS
from pymongo import MongoClient
from functools import wraps
from urllib.parse import unquote
from zoneinfo import ZoneInfo
import datetime
import math
import os
import hmac
import re

USING_DB = True
DIST_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../AirQualityMonitor-Frontend/dist")

API_KEY = os.environ.get('HAZELWOOD_API_KEY')
if not API_KEY:
    raise RuntimeError("HAZELWOOD_API_KEY environment variable not set")

app = Flask(__name__)
CORS(app, origins=["https://artsexcursionairquality.org","http://localhost:5173"])
api = Blueprint("api", __name__, url_prefix="/api")

print("Starting Flask app...\n\n")
if USING_DB:
    print("Connecting to database...")
    try:
        client = MongoClient("localhost", 27017, serverSelectionTimeoutMS=5000)
        # Force a round-trip
        client.admin.command('ping')
        db = client.Hazelwood
        app.logger.info("Connected to database!")

        if "Sensor Data" not in db.list_collection_names():
            db.create_collection("Sensor Data")
            app.logger.info("Created collection 'Sensor Data'")

        if "Devices" not in db.list_collection_names():
            db.create_collection("Devices")
            app.logger.info("Created collection 'Devices'")
    except (ServerSelectionTimeoutError, ConnectionFailure) as e:
        app.logger.error(f"Could not reach MongoDB: {e}")
        raise SystemExit(1)

SCANNER_PATTERNS = re.compile(
    r"(\.php|\.git|\.env|\.aws|wp-admin|wp-login|phpunit|"
    r"vendor/|/admin|amplify|terraform|acme-challenge|"
    r"eval-stdin|/cgi-bin|\.well-known/(?!acme))",
    re.IGNORECASE,
)

@app.before_request
def block_scanners():
    path = request.path
    try:
        decoded = unquote(path).lower()
    except Exception:
        decoded = path.lower()

    if SCANNER_PATTERNS.search(decoded):
        return "", 444

    print(f"[REQUEST] {request.method} {request.path} from {request.headers.get('CF-Connecting-IP', request.remote_addr)}")

valid_measurement_types = [
    "pm25_standard",
    "pm100_standard",
    "aqi_pm25",
    "aqi_pm100",
    "aqi_pm03um",
    "temperature",
    "humidity",
]

def process_data(device_name, sensor_type, value):
    datapoint = {
        "timestamp": datetime.datetime.now(datetime.timezone.utc),
        "measurement_type": sensor_type,
        "sensor_value": value,
        "device_name": device_name,
    }
    app.logger.info(f"Received {sensor_type} data: {value}")
    app.logger.info(f"Datapoint: {datapoint}")
    if not USING_DB:
        return datapoint
    # Add AQI data to database
    return db["Sensor Data"].insert_one(datapoint)

def avg_for_type(device, measurement_type, n=10):
    if not USING_DB:
        raise RuntimeError("avg_for_type requires a database connection")
    
    docs = list(
        db["Sensor Data"]
        .find({"measurement_type": measurement_type, "device_name": device})
        .sort("timestamp", -1)
        .limit(n)
    )
    return docs, (sum(d["sensor_value"] for d in docs) / len(docs) if docs else 0)

def fuzz_coord(value, precision=3):
    "Round coordinates for privacy. 3 decimals ≈ 110m, 4 ≈ 11m."
    if value is None:
        return None
    return round(value, precision)

def convert_utc_to_est(utc_time):
  #fetch the timezone information 
  if utc_time.tzinfo is None:
    utc_time = utc_time.replace(tzinfo=datetime.timezone.utc)

  return utc_time.astimezone(ZoneInfo("America/New_York"))    

@api.get("/sensor_data")
def index_get():
    if not USING_DB:
        return "No data available"

    # Get all devices in the database
    devices = db["Sensor Data"].distinct("device_name")
    print(f"Devices: {devices}")
    points = []
    for device in devices:
        print(f"Device: {device}")

        aqi_docs,  quality     = avg_for_type(device, "aqi_pm25")
        pm03_docs, pm03um      = avg_for_type(device, "aqi_pm03um")
        temp_docs, temperature = avg_for_type(device, "temperature")
        hum_docs,  humidity    = avg_for_type(device, "humidity")

        quality = round(quality)
        pm03um = round(pm03um)

        print(f"Average AQI: {quality}")
        print(f"Average Pm03um: {pm03um}")
        print(f"Average Temperature: {temperature}")
        print(f"Average humidity: {humidity}")

        dev = db["Devices"].find_one({"device_name": device})
        if dev is None:
            print(f"Device {device} not found in database")
            continue

        # Get the most recent timestamp from the readings
        most_recent_timestamp = aqi_docs[0]["timestamp"] if aqi_docs else None

        point = {
            "device_name": device,
            "lon": fuzz_coord(dev["long"]),
            "lat": fuzz_coord(dev["lat"]),
            "device_quality": quality,
            "particle_03um": pm03um,
            "temperature": temperature,
            "humidity": humidity,
            "timestamp": most_recent_timestamp.isoformat() if isinstance(most_recent_timestamp, datetime.datetime) else most_recent_timestamp
        }
        points.append(point)

    #return render_template("homepage.html", points=points)
    return jsonify(points)


@app.get("/sensor_data")
def sensor_data_compat():
    return index_get()


def require_api_key(f):
    @wraps(f)
    def decorated(*args, **kwargs):
        provided = request.headers.get('X-API-Key', '')
        # hmac.compare_digest prevents timing attacks
        if not hmac.compare_digest(provided, API_KEY):
            abort(401)
        return f(*args, **kwargs)
    return decorated

@api.post("/sensor_data/<device_name>")
@require_api_key
def process_sensor_data(device_name):
    data = request.get_json()

    # Register device if new
    if db["Devices"].count_documents({"device_name": device_name}) == 0:
        print(f"Device {device_name} not found in database")
        print("Adding device to database")
        db["Devices"].insert_one(
            {"device_name": device_name, "lat": fuzz_coord(data.get("lat")), "long": fuzz_coord(data.get("long"))}
        )
        print(f"Device {device_name} added to database")

    # New unified format: has temperature/humidity/aqi fields directly
    if "temperature" in data or "humidity" in data or "aqi_pm25" in data or "aqi_pm100" in data or "aqi_pm03um" in data:
        measurements = {
            "temperature": data.get("temperature"),
            "humidity": data.get("humidity"),
            "aqi_pm25": data.get("aqi_pm25"),
            "aqi_pm100": data.get("aqi_pm100"),
            "aqi_pm03um": data.get("aqi_pm03um")
        }
        for measurement_type, value in measurements.items():
            if value is not None:
                print(f"Received {measurement_type} data: {value}")
                process_data_response = process_data(device_name, measurement_type, value)
                print(f"process_data_response: {process_data_response}")
        return "OK"

    return "Missing measurement fields", 400

@api.get("/sensor_data/<device_name>")
def show_sensor_data(device_name):
    if not USING_DB:
        return "No data available"
    
    try:
        limit = int(request.args.get("limit", 10))
    except ValueError:
        return "Invalid limit", 400
    limit = max(1, min(limit, 10000))

    data = []

    for measurement_type in valid_measurement_types:
        last_ten = list(
            db["Sensor Data"]
            .find({"measurement_type": measurement_type, "device_name": device_name})
            .sort("timestamp", -1)
            .limit(limit)
        )

        if last_ten:
            values = [
                {
                    "value": doc["sensor_value"],
                    "timestamp": convert_utc_to_est(doc["timestamp"]).isoformat() if
                    isinstance(doc["timestamp"], datetime.datetime) else 
                    doc["timestamp"]    
                }
                for doc in last_ten
                if "sensor_value" in doc
            ]
            data.append({"type": measurement_type, "values": values})    
        else:
            print(f"No {measurement_type} data available")
            data.append({"type": measurement_type, "values": []})

    print("Averages:", data)
    #return render_template("sensor_data.html", data=data)
    return jsonify(data)


#Register blueprint
app.register_blueprint(api)

@app.get("/")
def serve_frontend():
    return send_from_directory(DIST_DIR, "index.html")

@app.route("/<path:path>")
def serve_static(path):
    if path.startswith("api/"):
        return {"error": "Not found"}, 404
    full_path = os.path.join(DIST_DIR, path)
    if os.path.exists(full_path):
        return send_from_directory(DIST_DIR, path)
    return send_from_directory(DIST_DIR, "index.html")