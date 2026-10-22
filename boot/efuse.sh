# Shared readers for boot/otp.efuse. The hash is SHA-256 of the PKCS#1 RSA public key, DER encoded.
# That is the same byte string TF-A embeds in BL1 as the ROTPK hash.

efuse_path() {
	local root="$1"
	printf '%s\n' "$root/boot/otp.efuse"
}

efuse_read() {
	local file="$1"
	EFUSE_BURNED=0
	EFUSE_HASH=
	EFUSE_NOTES=()
	local line
	while IFS= read -r line || [[ -n "$line" ]]; do
		case "$line" in
		\#*)
			EFUSE_NOTES+=("$line")
			;;
		burned=*)
			EFUSE_BURNED="${line#burned=}"
			EFUSE_BURNED="${EFUSE_BURNED//$'\r'/}"
			;;
		hash=*)
			EFUSE_HASH="${line#hash=}"
			EFUSE_HASH="${EFUSE_HASH//$'\r'/}"
			;;
		esac
	done <"$file"
}

efuse_write() {
	local file="$1" burned="$2" hash="$3"
	local tmp
	tmp=$(mktemp)
	local note
	for note in "${EFUSE_NOTES[@]}"; do
		printf '%s\n' "$note" >>"$tmp"
	done
	printf 'burned=%s\nhash=%s\n' "$burned" "$hash" >>"$tmp"
	mv "$tmp" "$file"
}

# SHA-256 hex of the PKCS#1 DER public key. Private PEM or public PEM.
rotpk_hash_pem() {
	local pem="$1" der hash
	der=$(mktemp)
	if grep -q 'PRIVATE KEY' "$pem"; then
		openssl rsa -in "$pem" -pubout -outform DER -out "$der"
	else
		openssl rsa -pubin -in "$pem" -outform DER -out "$der"
	fi
	hash=$(openssl dgst -sha256 "$der" | awk '{print $NF}')
	rm -f "$der"
	printf '%s\n' "$hash"
}
