#!/usr/bin/env python3
"""Create the local IoT database and its schema."""

import argparse
import getpass
from pathlib import Path

try:
    import psycopg2
    from psycopg2 import sql
except ImportError as error:
    raise SystemExit(
        "psycopg2 is required. Install it with: "
        "python -m pip install psycopg2-binary"
    ) from error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5432)
    parser.add_argument("--user", default="postgres")
    parser.add_argument("--database", default="iot_dashboard")
    args = parser.parse_args()

    password = getpass.getpass("PostgreSQL password: ")
    admin_conn = None
    app_conn = None

    try:
        admin_conn = psycopg2.connect(
            host=args.host,
            port=args.port,
            user=args.user,
            password=password,
            dbname="postgres",
            connect_timeout=5,
        )
        admin_conn.autocommit = True
        with admin_conn.cursor() as cursor:
            cursor.execute(
                "SELECT 1 FROM pg_database WHERE datname = %s",
                (args.database,),
            )
            if cursor.fetchone() is None:
                cursor.execute(
                    sql.SQL("CREATE DATABASE {}").format(
                        sql.Identifier(args.database)
                    )
                )
                print("Created database: {}".format(args.database))
            else:
                print("Database already exists: {}".format(args.database))
    except psycopg2.Error as error:
        print("Could not create or inspect the database: {}".format(error))
        return 1
    finally:
        if admin_conn is not None:
            admin_conn.close()

    schema_file = Path(__file__).with_name("create_iot_schema.sql")
    app_conn = None
    try:
        app_conn = psycopg2.connect(
            host=args.host,
            port=args.port,
            user=args.user,
            password=password,
            dbname=args.database,
            connect_timeout=5,
        )
        with schema_file.open("r", encoding="utf-8") as file:
            schema = file.read()
        with app_conn.cursor() as cursor:
            cursor.execute(schema)
        app_conn.commit()
        print("Created IoT tables and views in: {}".format(args.database))
        return 0
    except (OSError, psycopg2.Error) as error:
        if app_conn is not None:
            app_conn.rollback()
        print("Could not initialize the IoT schema: {}".format(error))
        return 1
    finally:
        if app_conn is not None:
            app_conn.close()


if __name__ == "__main__":
    raise SystemExit(main())
