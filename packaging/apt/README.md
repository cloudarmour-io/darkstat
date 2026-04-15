# APT Packaging (Branch: packaging/apt)

This branch contains Debian packaging for `darkstat` with PR #12 JSON support.

## Build `.deb` on Debian/Ubuntu

```bash
sudo apt update
sudo apt install -y build-essential devscripts debhelper dh-autoreconf autoconf automake libpcap-dev zlib1g-dev

# from repo root
dpkg-buildpackage -us -uc -b
```

Artifacts will be generated in the parent directory, for example:

- `../darkstat_3.0.722+git20260415-1_amd64.deb`

## Install and run

```bash
sudo apt install -y ../darkstat_*_amd64.deb
sudoedit /etc/default/darkstat   # set INTERFACE
sudo systemctl enable --now darkstat
```

## Validate JSON endpoint

```bash
curl -i http://127.0.0.1:667/json
curl -s http://127.0.0.1:667/json | jq .
```

## Publish in your own APT repo

Use `aptly` or `reprepro` to publish the generated `.deb`, then distribute:

```bash
echo "deb [signed-by=/usr/share/keyrings/cloudarmour.gpg] https://<your-apt-url> stable main" | sudo tee /etc/apt/sources.list.d/cloudarmour.list
sudo apt update
sudo apt install darkstat
```
