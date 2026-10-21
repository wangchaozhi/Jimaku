#!/bin/bash
# 把钥匙串里的自签名代码签名证书写进 GitHub 仓库 secrets，供 CI 给 macOS 正式版签名。
# 用法：tools/set-signing-secrets.sh ["证书名"] [owner/repo]
#   默认：证书 "SnapPin Release"，仓库 wangchaozhi/Jimaku
# 私钥和密码只在内存和临时目录里出现，结束时删除，不会打印出来。
set -euo pipefail

IDENTITY="${1:-SnapPin Release}"
REPO="${2:-wangchaozhi/Jimaku}"
OPENSSL=/usr/bin/openssl # 系统自带的 LibreSSL：生成的 p12 用 macOS 能直接导入的算法

umask 077
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

command -v gh > /dev/null || { echo "需要 GitHub CLI：brew install gh"; exit 1; }
gh auth status > /dev/null 2>&1 || { echo "先登录：gh auth login"; exit 1; }
security find-identity -p codesigning | grep -q "\"$IDENTITY\"" || { echo "登录钥匙串里没有签名身份 \"$IDENTITY\""; exit 1; }

# 1. 导出登录钥匙串里的所有签名身份（security 不能只导出一个）。macOS 会为每个私钥弹窗确认
export EXPORT_PASS=$($OPENSSL rand -hex 24)
echo "正在导出（每个私钥都会弹窗：输入登录密码并点“允许”）…"
security export -k "$HOME/Library/Keychains/login.keychain-db" -t identities -f pkcs12 -P "$EXPORT_PASS" -o "$WORK/all.p12"
$OPENSSL pkcs12 -in "$WORK/all.p12" -passin env:EXPORT_PASS -nodes -out "$WORK/all.pem" 2> /dev/null

# 2. 只留下目标证书和与它 localKeyID 相同的私钥
IDENTITY="$IDENTITY" WORK="$WORK" python3 - << 'EOF'
import os, re
work, name = os.environ["WORK"], os.environ["IDENTITY"]
text = open(f"{work}/all.pem").read()
blocks = re.findall(r"Bag Attributes(.*?)(-----BEGIN ([A-Z ]+)-----.*?-----END \3-----)", text, re.S)
def attr(head, key):
    m = re.search(key + r":\s*(.+)", head)
    return m.group(1).strip() if m else None
cert = next((b for b in blocks if b[2] == "CERTIFICATE" and attr(b[0], "friendlyName") == name), None)
if not cert:
    raise SystemExit(f"导出的内容里找不到证书 {name}")
kid = attr(cert[0], "localKeyID")
key = next((b for b in blocks if "PRIVATE KEY" in b[2] and attr(b[0], "localKeyID") == kid), None)
if not key:
    raise SystemExit(f"找不到 {name} 的私钥")
open(f"{work}/cert.pem", "w").write(cert[1] + "\n")
open(f"{work}/key.pem", "w").write(key[1] + "\n")
EOF

# 3. 重新打包成只含这一个身份的 p12（新的随机密码）并检查能否读回
export P12_PASS=$($OPENSSL rand -hex 24)
$OPENSSL pkcs12 -export -in "$WORK/cert.pem" -inkey "$WORK/key.pem" -name "$IDENTITY" \
    -passout env:P12_PASS -out "$WORK/signing.p12"
$OPENSSL pkcs12 -in "$WORK/signing.p12" -passin env:P12_PASS -nokeys -noout

# 4. 写进仓库 secrets（经标准输入传递，不出现在命令行参数里）
base64 -i "$WORK/signing.p12" | gh secret set MACOS_SIGNING_P12 -R "$REPO"
printf '%s' "$P12_PASS" | gh secret set MACOS_SIGNING_P12_PASSWORD -R "$REPO"
echo "完成：$REPO 已设置 MACOS_SIGNING_P12 / MACOS_SIGNING_P12_PASSWORD（证书 \"$IDENTITY\"）"
