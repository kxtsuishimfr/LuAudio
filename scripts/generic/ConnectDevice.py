import os
import subprocess
from pathlib import Path
from dotenv import load_dotenv

script_path = Path(__file__).resolve()
root_dir = script_path.parents[2]
env_path = root_dir / "private" / "protected" / ".env"

if env_path.exists():
    load_dotenv(dotenv_path=env_path)
else:
    print(f"Error: Could not find .env file at expected path: {env_path}")
    exit(1)

IP_PORT = os.getenv("IP_PORT")
PAIRING_CODE = os.getenv("PAIRING_CODE")

def run_adb_pair():
    if not IP_PORT or not PAIRING_CODE:
        print("Error: IP_PORT or PAIRING_CODE is missing from the .env file.")
        return

    print("=" * 40)
    print(f" YOUR PAIRING CODE IS: {PAIRING_CODE}")
    print("=" * 40)
    print(f"Connecting to: {IP_PORT}\n")
    
    subprocess.run(['adb', 'pair', IP_PORT])

if __name__ == "__main__":
    run_adb_pair()
