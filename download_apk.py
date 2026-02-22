import urllib.request
import json
import zipfile
import struct
import io

def get_latest_apk_url():
    req = urllib.request.Request("https://api.github.com/repos/KernelSU-Next/KernelSU-Next/releases/latest")
    with urllib.request.urlopen(req) as response:
        data = json.loads(response.read().decode())
        for asset in data['assets']:
            if asset['name'].endswith('.apk') and 'Manager' in asset['name'] and 'debug' not in asset['name']:
                return asset['browser_download_url']
    return None

def main():
    url = get_latest_apk_url()
    if not url:
        print("Could not find latest APK URL.")
        return
    
    print(f"Downloading {url}...")
    req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
    with urllib.request.urlopen(req) as response:
        apk_data = response.read()
    
    with open('/tmp/ksu.apk', 'wb') as f:
        f.write(apk_data)
    
    print("Downloaded /tmp/ksu.apk")

if __name__ == '__main__':
    main()
