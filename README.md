# Air Quality Monitoring System - Backend

RESTful API server for community air quality monitoring, built in collaboration with Arts Excursion Unlimited and Carnegie Mellon University's Social Haptics Robotics & Education Lab.

## Overview

This Flask-based backend runs on a Raspberry Pi, receiving data from distributed air quality sensors and serving it to the web dashboard. The system collects temperature, humidity, and PM2.5/PM10 AQI readings from custom-built monitoring units deployed throughout Pittsburgh's Greater Hazelwood neighborhoods.

## Features

- **RESTful API** - Clean endpoints for sensor data ingestion and retrieval
- **MongoDB Integration** - Time-series data storage for environmental monitoring
- **Device Registration** - Automatic registration of new sensor devices with location data
- **CORS Support** - Cross-origin resource sharing for frontend integration
- **Cloudflare Tunnel** - Secure public access via cloudflared

## Tech Stack

- **Flask** - Python web framework with Flask-CORS
- **MongoDB** - NoSQL database for time-series data
- **PyMongo** - MongoDB driver for Python
- **Cloudflare Tunnel** - Secure tunneling for public access
- **Python 3.x** - Core programming language

## System Architecture

```
Air Quality Sensors (Arduino with WiFi)
    ↓ [WiFi - HTTP POST]
Raspberry Pi Backend (Flask Server)
    ↓ [Flask → PyMongo]
MongoDB Database (localhost:27017)
    ↓ [Cloudflare Tunnel]
Public Internet
    ↓ [REST API - JSON]
React Frontend Dashboard
```

## Getting Started

### Prerequisites

- Python 3.x
- MongoDB
- Raspberry Pi (recommended) or any Linux server
- Cloudflare account (for tunnel setup)

### Installation

```bash
# Clone the repository
git clone https://github.com/PenguinMesssiah/HazelwoodServer
cd HazelwoodServer

# Install dependencies
pip install -r requirements.txt
```

**Dependencies** ([requirements.txt](requirements.txt)):
- `python3-flask` - Web framework
- `pymongo` - MongoDB driver
- `numpy` - Numerical computing (for data processing)

### MongoDB Setup

```bash
# Install MongoDB (Ubuntu/Debian)
sudo apt-get update
sudo apt-get install -y mongodb

# Start MongoDB service
sudo systemctl start mongodb
sudo systemctl enable mongodb

# Verify MongoDB is running
sudo systemctl status mongodb
```

The application will automatically:
- Connect to MongoDB at `localhost:27017`
- Create the `Hazelwood` database
- Create `Sensor Data` and `Devices` collections if they don't exist

### Running the Server

#### Development Mode

Use the provided launch script:

```bash
# Make script executable (first time only)
chmod +x launch.sh

# Run the development server
./launch.sh
```

Or manually with Flask:

```bash
# Run Flask development server
flask run --debug
```

#### Production Mode

The server runs on port 8000 via [wsgi.py](wsgi.py):

```bash
# Run with Python WSGI
python wsgi.py
```

The application is configured to accept CORS requests from:
- `https://artsexcursionairquality.org`
- `http://localhost:5173`

## API Endpoints

### Root / Overview

**GET** `/` or **GET** `/sensor_data`

Returns aggregated data for all devices with recent AQI averages and locations.

**Response:**
```json
[
  {
    "device_name": "sensor_001",
    "lon": -79.9384,
    "lat": 40.4259,
    "device_quality": 50,
    "timestamp": "2026-04-02T15:30:00"
  }
]
```

### Data Ingestion

**POST** `/sensor_data/<device_name>`

Submit sensor readings from a monitoring unit. The endpoint supports two formats:

**Unified Format (Preferred):**
```json
{
  "lat": 40.4259,
  "long": -79.9384,
  "temperature": 22.5,
  "humidity": 45.2,
  "aqi_pm25": 35,
  "aqi_pm100": 28
}
```

**Legacy Format:**
```json
{
  "lat": 40.4259,
  "long": -79.9384,
  "measurement_type": "temperature",
  "value": 22.5
}
```

Valid measurement types: `pm25_standard`, `pm100_standard`, `aqi_pm25`, `aqi_pm100`, `temperature`, `humidity`

### Data Retrieval

**GET** `/sensor_data/<device_name>`

Get the last 10 readings for each measurement type from a specific device.

**Response:**
```json
[
  {
    "type": "aqi_pm25",
    "values": [
      {"value": 35, "timestamp": "2026-04-02T15:30:00"},
      {"value": 32, "timestamp": "2026-04-02T15:20:00"}
    ]
  },
  {
    "type": "temperature",
    "values": [
      {"value": 22.5, "timestamp": "2026-04-02T15:30:00"}
    ]
  }
]
```

## Project Structure

```
HazelwoodServer/
├── app.py                      # Main Flask application with all routes
├── wsgi.py                     # WSGI entry point (port 8000)
├── launch.sh                   # Development server launcher
├── requirements.txt            # Python dependencies
├── config.yml                  # Cloudflare tunnel configuration
├── HazelwoodSendData/         # Arduino sensor code
│   ├── HazelwoodSendData.ino  # Arduino sketch
│   └── arduino_secrets.h      # WiFi credentials (gitignored)
├── templates/                  # HTML templates (legacy)
│   ├── homepage.html
│   └── sensor_data.html
├── static/                     # Static files
│   └── style.css
├── logs/                       # Application logs
│   └── logs.txt
└── time_fetch.js              # Utility script
```

## Database Schema

**Database:** `Hazelwood`

### Collection: `Sensor Data`

Stores individual measurements from devices:

```javascript
{
  "_id": ObjectId("..."),
  "timestamp": ISODate("2026-04-02T15:30:00"),
  "measurement_type": "aqi_pm25",  // or temperature, humidity, etc.
  "sensor_value": 35.0,
  "device_name": "sensor_001"
}
```

### Collection: `Devices`

Stores device registration and location data:

```javascript
{
  "_id": ObjectId("..."),
  "device_name": "sensor_001",
  "lat": 40.4259,
  "long": -79.9384
}
```

**Note:** Devices are automatically registered on first data submission.

## Deployment on Raspberry Pi

### Install System Dependencies

```bash
sudo apt-get update
sudo apt-get install -y python3-pip mongodb
```

### Cloudflare Tunnel Setup

The server uses Cloudflare Tunnel for secure public access (see [config.yml](config.yml)).

1. Install cloudflared:
```bash
wget https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-arm64.deb
sudo dpkg -i cloudflared-linux-arm64.deb
```

2. Authenticate with Cloudflare:
```bash
cloudflared tunnel login
```

3. Update [config.yml](config.yml) with your credentials:
```yaml
tunnel: your-tunnel-name
credentials-file: /path/to/credentials.json

ingress:
  - hostname: yourdomain.org
    service: http://127.0.0.1:8000
  - service: http_status:404
```

4. Run the tunnel:
```bash
cloudflared tunnel --config config.yml run
```

### Running as a Service

You can create systemd services for both the Flask app and Cloudflare tunnel to run on boot.

## Testing

```bash
# Test overview endpoint
curl http://localhost:8000/sensor_data

# Test device-specific data
curl http://localhost:8000/sensor_data/sensor_001

# Submit test data
curl -X POST http://localhost:8000/sensor_data/test_device \
  -H "Content-Type: application/json" \
  -d '{
    "lat": 40.4259,
    "long": -79.9384,
    "temperature": 22.5,
    "humidity": 45.2,
    "aqi_pm25": 35
  }'
```

## Arduino Sensor Code

The [HazelwoodSendData/](HazelwoodSendData/) directory contains the Arduino sketch for the sensor hardware:
- WiFi-enabled microcontroller (compatible with Arduino)
- PM2.5/PM10 particulate matter sensor
- Temperature and humidity sensor
- Sends data via HTTP POST to the Flask server

## Contributing

This project welcomes community contributions!

## Acknowledgments

- **Arts Excursion Unlimited** - Community partnership and workshop facilitation
- **Carnegie Mellon University SHARE Lab** - Technical guidance and research support
- **Greenfield, Hazelwood, and Glen-Hazel Community Members** - Active participation and invaluable feedback

## Related Repositories

- [Frontend Repository](https://github.com/PenguinMesssiah/AirQualityMontior-Frontend) - React dashboard

## Resources

- [EPA AQI Guidelines](https://www.airnow.gov/aqi/aqi-basics/)
- [Flask Documentation](https://flask.palletsprojects.com/)
- [PyMongo Documentation](https://pymongo.readthedocs.io/)
- [Cloudflare Tunnel Documentation](https://developers.cloudflare.com/cloudflare-one/connections/connect-apps/)

## Contact

For questions or collaboration opportunities, please open an issue or contact the project maintainers.

---

*Built with love by the Greater Hazelwood community*
