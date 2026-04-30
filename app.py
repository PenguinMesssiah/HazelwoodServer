from flask import Flask, Blueprint, request, render_template, jsonify
from flask_cors import CORS
from pymongo import MongoClient
import datetime
import math
import json

USING_DB = True

app = Flask(__name__)
CORS(app, origins=["https://artsexcursionairquality.org","http://localhost:5173"])
api = Blueprint("api", __name__, url_prefix="/api")

@app.before_request
def log_request_info():
    print(f"[REQUEST] {request.method} {request.path} from {request.remote_addr}")

print("Starting Flask app...\n\n")
if USING_DB:
    print("Connecting to database...")
    # Connecting to database
    client = MongoClient("localhost", 27017)
    db = client.Hazelwood
    # Confirm the connection
    if db is not None:
        print("Connected to database!")
        # Create collection if it doesn't exist
        if "Sensor Data" not in db.list_collection_names():
            db.create_collection("Sensor Data")
            print("Created collection 'Sensor Data'")

        if "Devices" not in db.list_collection_names():
            db.create_collection("Devices")
            print("Created collection 'Devices'")

    else:
        print("Failed to connect to database")
        exit(1)


valid_measurement_types = [
    "pm25_standard",
    "pm100_standard",
    "aqi_pm25",
    "aqi_pm100",
    "temperature",
    "humidity",
]


def process_data(device_name, sensor_type, value):
    datapoint = {
        "timestamp": datetime.datetime.now(),
        "measurement_type": sensor_type,
        "sensor_value": value,
        "device_name": device_name,
    }
    print(f"Received {sensor_type} data: {value}")
    print(f"Datapoint: {datapoint}")
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
        temp_docs, temperature = avg_for_type(device, "temperature")
        hum_docs,  humidity    = avg_for_type(device, "humidity")

        print(f"Average AQI: {quality}")
        print(f"Average Temperature: {temperature}")
        print(f"Average humidity: {humidity}")

        if quality > 0:
            quality = math.ceil(quality / 10) * 10
        else:
            quality = 0
        print(f"Rounded AQI: {quality}")

        dev = db["Devices"].find_one({"device_name": device})
        if dev is None:
            print(f"Device {device} not found in database")
            continue

        # Get the most recent timestamp from the readings
        most_recent_timestamp = aqi_docs[0]["timestamp"] if aqi_docs else None

        point = {
            "device_name": device,
            "lon": dev["long"],
            "lat": dev["lat"],
            "device_quality": quality,
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


@api.post("/sensor_data/<device_name>")
def process_sensor_data(device_name):
    data = request.get_json()

    # Register device if new
    if db["Devices"].count_documents({"device_name": device_name}) == 0:
        print(f"Device {device_name} not found in database")
        print("Adding device to database")
        db["Devices"].insert_one(
            {"device_name": device_name, "lat": data.get("lat"), "long": data.get("long")}
        )
        print(f"Device {device_name} added to database")

    # New unified format: has temperature/humidity/aqi fields directly
    if "temperature" in data or "humidity" in data or "aqi_pm25" in data or "aqi_pm100" in data:
        measurements = {
            "temperature": data.get("temperature"),
            "humidity": data.get("humidity"),
            "aqi_pm25": data.get("aqi_pm25"),
            "aqi_pm100": data.get("aqi_pm100"),
        }
        for measurement_type, value in measurements.items():
            if value is not None:
                print(f"Received {measurement_type} data: {value}")
                process_data_response = process_data(device_name, measurement_type, value)
                print(f"process_data_response: {process_data_response}")
        return "OK"

    # Legacy single-measurement format
    if "measurement_type" not in data:
        print("Missing type")
        return "Missing value", 400
    if "value" not in data:
        print("Missing value")
        return "Missing value", 400

    measurement_type = data["measurement_type"]
    if measurement_type not in valid_measurement_types:
        print(f"Invalid measurement type: {measurement_type}")
        return "Invalid measurement type", 400

    value = data["value"]
    print(f"Received {measurement_type} data: {value}")
    process_data_response = process_data(device_name, measurement_type, value)
    print(f"process_data_response: {process_data_response}")
    return "OK"


@api.get("/sensor_data/<device_name>")
def show_sensor_data(device_name):
    if not USING_DB:
        return "No data available"

    data = []

    for measurement_type in valid_measurement_types:
        last_ten = list(
            db["Sensor Data"]
            .find({"measurement_type": measurement_type, "device_name": device_name})
            .sort("timestamp", -1)
            .limit(10)
        )

        if last_ten:
            values = [
                {
                    "value": doc["sensor_value"],
                    "timestamp": doc["timestamp"].isoformat() if
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