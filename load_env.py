from pathlib import Path

Import("env")

project_dir = Path(env.subst("$PROJECT_DIR"))
env_file = project_dir / ".env"
required = ("WIFI_SSID", "WIFI_PASSWORD", "THINGSPEAK_WRITE_API_KEY")

if not env_file.is_file():
    raise RuntimeError("Missing .env; copy .env.example and fill in the required values.")

values = {}
for line in env_file.read_text(encoding="utf-8-sig").splitlines():
    line = line.strip()
    if not line or line.startswith("#"):
        continue
    if "=" not in line:
        raise ValueError("Invalid .env line: expected KEY=value.")
    key, value = line.split("=", 1)
    key = key.strip()
    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in ("'", '"'):
        value = value[1:-1]
    if key in values:
        raise ValueError("Duplicate .env key: " + key)
    values[key] = value

missing = [key for key in required if not values.get(key)]
if missing:
    raise ValueError("Missing required .env values: " + ", ".join(missing))

api_key = values["THINGSPEAK_WRITE_API_KEY"]
if not api_key.isascii() or not api_key.isalnum():
    raise ValueError("THINGSPEAK_WRITE_API_KEY must contain only ASCII letters and digits.")

def c_string(value):
    return '"' + "".join("\\%03o" % byte for byte in value.encode("utf-8")) + '"'

build_dir = Path(env.subst("$BUILD_DIR"))
build_dir.mkdir(parents=True, exist_ok=True)
header = build_dir / "local_config.h"
content = "#pragma once\n" + "".join(
    "#define CONFIG_" + key + " " + c_string(values[key]) + "\n"
    for key in required
)
if not header.exists() or header.read_text(encoding="utf-8") != content:
    header.write_text(content, encoding="utf-8")
env.Append(CPPPATH=[str(build_dir)])
