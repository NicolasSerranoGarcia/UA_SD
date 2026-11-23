import argparse
import sqlite3
from .store import database_path, initialize, reset_enrollment


def main():
    parser = argparse.ArgumentParser(prog="wm-registry")
    commands = parser.add_subparsers(dest="command", required=True)
    reset = commands.add_parser("reset-enrollment", help="Clear an enrollment when the credential is lost")
    reset.add_argument("--station-id", type=int, required=True)
    args = parser.parse_args()
    path = database_path()
    try:
        initialize(path)
        if reset_enrollment(path, args.station_id):
            print("Enrollment cleared. The station can register again.")
        else:
            print("The station has no enrollment to clear.")
    except ValueError as error:
        parser.error(str(error))
    except sqlite3.Error:
        parser.error("Registry database is unavailable.")


if __name__ == "__main__":
    main()
