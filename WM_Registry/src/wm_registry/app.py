import json
import logging
import sqlite3
from datetime import datetime, timezone

from flask import Flask, jsonify, request
from werkzeug.exceptions import RequestEntityTooLarge

from . import store


audit = logging.getLogger("wm_registry.audit")
if not audit.handlers:
    audit.addHandler(logging.StreamHandler())
audit.setLevel(logging.INFO)
audit.propagate = False


def create_app(path=None):
    app = Flask(__name__)
    path = path if path is not None else store.database_path()
    store.initialize(path)

    @app.after_request
    def protect_and_log(response):
        response.headers["Cache-Control"] = "no-store"
        audit.info(json.dumps({
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "source_ip": request.remote_addr,
            "station_id": (request.view_args or {}).get("station_id"),
            "action": {"PUT": "enroll", "DELETE": "revoke", "GET": "verify"}.get(request.method, "verify"),
            "status": response.status_code,
        }))
        return response

    @app.errorhandler(RequestEntityTooLarge)
    def too_large(_error):
        return jsonify(error="Registry request is too large."), 400

    @app.route("/api/registry/stations/<int:station_id>", methods=["PUT", "GET", "DELETE"])
    def station(station_id):
        if not store.valid_station_id(station_id):
            return jsonify(error="Station ID must be a positive SQLite integer."), 400
        try:
            if request.method == "PUT":
                request.max_content_length = 4096
                body = request.get_json(silent=True)
                if not isinstance(body, dict) or not store.valid_city(body.get("city")):
                    return jsonify(error="Supply a lowercase city slug."), 400
                city = body["city"]
                credential = store.enroll(path, station_id, city)
                if credential is None:
                    return jsonify(error="Station ID is already registered."), 409
                return jsonify(station_id=station_id, city=city, credential=credential), 201

            auth = request.authorization
            token = auth.token if auth and auth.type == "bearer" else None
            if not token:
                return jsonify(error="Registry authorization was denied."), 401
            if request.method == "DELETE":
                if store.revoke(path, station_id, token):
                    return "", 204
            else:
                city = store.verify_station(path, station_id, token)
                if city is not None:
                    return jsonify(station_id=station_id, city=city)
        except sqlite3.Error:
            return jsonify(error="Registry database is unavailable. Try again."), 503
        return jsonify(error="Registry authorization was denied."), 401

    return app
