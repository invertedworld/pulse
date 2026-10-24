# Notarization credentials, resolved the same way as rxdoctor's scripts/notary-auth.sh.
#
# Sourced, not executed. Sets two variables:
#
#   NOTARY_AUTH   array of notarytool arguments
#   NOTARY_DESC   human-readable description, for logging
#
# Precedence, most explicit first:
#
#   1. KEYCHAIN_PROFILE                              a stored notarytool profile
#   2. NOTARY_KEY_ID / NOTARY_ISSUER / NOTARY_KEY    App Store Connect API key
#   3. APPLE_ID / APPLE_TEAM_ID / APP_SPECIFIC_PASSWORD
#
# The API key is the default, and the only one with defaults. Keychain profiles holding
# an app-specific password keep going stale: an Apple ID password change revokes every
# app-specific password, and one profile stopped resolving mid-release for reasons never
# established. A .p8 on disk cannot fail that way. KEYCHAIN_PROFILE still wins when
# explicitly set, because someone who sets it means it.

# Not secret: the key id and issuer id identify the key; they authorise nothing without
# the .p8 itself, which lives outside this repository.
NOTARY_KEY_ID="${NOTARY_KEY_ID:-${AC_API_KEY_ID:-YUJKRV39B5}}"
NOTARY_ISSUER="${NOTARY_ISSUER:-${AC_API_ISSUER_ID:-af258c7e-593a-4b81-87ea-fb9cb06acbb5}}"
NOTARY_KEY="${NOTARY_KEY:-${AC_API_KEY_PATH:-$HOME/.appstoreconnect/private_keys/AuthKey_${NOTARY_KEY_ID}.p8}}"
KEYCHAIN_PROFILE="${KEYCHAIN_PROFILE:-}"

if [ -n "$KEYCHAIN_PROFILE" ]; then
    NOTARY_AUTH=(--keychain-profile "$KEYCHAIN_PROFILE")
    NOTARY_DESC="keychain profile '$KEYCHAIN_PROFILE'"
elif [ -f "$NOTARY_KEY" ]; then
    NOTARY_AUTH=(--key "$NOTARY_KEY" --key-id "$NOTARY_KEY_ID" --issuer "$NOTARY_ISSUER")
    NOTARY_DESC="API key $NOTARY_KEY_ID"
elif [ -n "${APPLE_ID:-}" ] && [ -n "${APPLE_TEAM_ID:-}" ] && [ -n "${APP_SPECIFIC_PASSWORD:-}" ]; then
    # Last resort: notarytool takes the password only as an argument, where `ps` shows it
    # to every account on the machine for as long as the submission runs.
    NOTARY_AUTH=(--apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" --password "$APP_SPECIFIC_PASSWORD")
    NOTARY_DESC="Apple ID $APPLE_ID (password visible in ps)"
else
    echo "error: no notarization credentials found." >&2
    echo "  looked for an App Store Connect key at:" >&2
    echo "    $NOTARY_KEY" >&2
    echo "  set NOTARY_KEY_ID / NOTARY_ISSUER / NOTARY_KEY to point elsewhere," >&2
    echo "  or KEYCHAIN_PROFILE=<name> to use a stored notarytool profile," >&2
    echo "  or APPLE_ID + APPLE_TEAM_ID + APP_SPECIFIC_PASSWORD." >&2
    exit 1
fi
