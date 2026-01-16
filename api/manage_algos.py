#!/usr/bin/env python3
import sqlite3
import sys
import os

DB_NAME = "api.db"

def init_db():
    conn = sqlite3.connect(DB_NAME)
    cursor = conn.cursor()
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS algorithms (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT UNIQUE NOT NULL,
            db_path TEXT NOT NULL,
            module_path TEXT NOT NULL
        )
    ''')
    conn.commit()
    conn.close()
    print(f"Database {DB_NAME} initialized.")

def add_algo(name, db_path, module_path):
    conn = sqlite3.connect(DB_NAME)
    cursor = conn.cursor()
    try:
        cursor.execute('''
            INSERT OR REPLACE INTO algorithms (name, db_path, module_path)
            VALUES (?, ?, ?)
        ''', (name, db_path, module_path))
        conn.commit()
        print(f"Algorithm '{name}' added/updated successfully.")
    except Exception as e:
        print(f"Error: {e}")
    finally:
        conn.close()

def list_algos():
    conn = sqlite3.connect(DB_NAME)
    cursor = conn.cursor()
    cursor.execute('SELECT name, db_path, module_path FROM algorithms')
    rows = cursor.fetchall()
    if not rows:
        print("No algorithms registered.")
    else:
        print(f"{'Name':<10} | {'DB Path':<20} | {'Module Path'}")
        print("-" * 60)
        for row in rows:
            print(f"{row[0]:<10} | {row[1]:<20} | {row[2]}")
    conn.close()

def main():
    if not os.path.exists(DB_NAME):
        init_db()

    if len(sys.argv) < 2:
        print("Usage:")
        print("  manage_algos.py list")
        print("  manage_algos.py add <name> <db_path> <module_path>")
        sys.exit(1)

    cmd = sys.argv[1]

    if cmd == "list":
        list_algos()
    elif cmd == "add":
        if len(sys.argv) != 5:
            print("Usage: manage_algos.py add <name> <db_path> <module_path>")
            sys.exit(1)
        add_algo(sys.argv[2], sys.argv[3], sys.argv[4])
    elif cmd == "init":
        init_db()
    else:
        print(f"Unknown command: {cmd}")

if __name__ == "__main__":
    main()
