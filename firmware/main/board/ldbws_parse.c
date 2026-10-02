#include "board/ldbws_parse.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"

#define MAX_REASON 60
#define MAX_MESSAGE 80
#define MAX_INPUT_MESSAGE 600   /* characters of an NRCC message considered before clipping */

/* ---- text helpers --------------------------------------------------------------------------- */

/* Copy src into dst: HTML-ish text becomes plain ASCII, whitespace collapsed and trimmed, bytes
 * >= 0x80 become '?', and anything longer than `limit` characters ends in "...". The panel font
 * has no glyphs beyond ASCII (see docs/design-decision.md). */
static void clean_copy(char *dst, size_t dst_n, const char *src, size_t limit, int strip_html)
{
    char tmp[MAX_INPUT_MESSAGE + 1];
    size_t t = 0;
    for (size_t i = 0; src && src[i] && t < sizeof tmp - 1; i++) {
        unsigned char c = (unsigned char)src[i];
        if (strip_html && c == '<') {                    /* drop a tag, leave a space where it was */
            while (src[i] && src[i] != '>') i++;
            c = ' ';
            if (!src[i]) { tmp[t++] = (char)c; break; }
        } else if (strip_html && c == '&') {             /* the entities LDBWS actually produces */
            static const struct { const char *e; char c; } ENT[] = {
                {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&#39;", '\''},
                {"&apos;", '\''}, {"&nbsp;", ' '},
            };
            for (size_t k = 0; k < sizeof ENT / sizeof ENT[0]; k++) {
                size_t el = strlen(ENT[k].e);
                if (!strncmp(src + i, ENT[k].e, el)) { c = (unsigned char)ENT[k].c; i += el - 1; break; }
            }
        }
        if (c >= 0x80) c = '?';
        if (c < 0x20) c = ' ';
        tmp[t++] = (char)c;
    }
    tmp[t] = '\0';

    char out[MAX_INPUT_MESSAGE + 1];
    size_t o = 0;
    int pending_space = 0;
    for (size_t i = 0; tmp[i]; i++) {
        if (tmp[i] == ' ') { pending_space = o > 0; continue; }
        if (pending_space) { out[o++] = ' '; pending_space = 0; }
        out[o++] = tmp[i];
    }
    out[o] = '\0';

    if (limit >= dst_n) limit = dst_n - 1;
    if (o > limit) {
        size_t keep = limit >= 3 ? limit - 3 : 0;
        while (keep > 0 && out[keep - 1] == ' ') keep--;
        memcpy(dst, out, keep);
        memcpy(dst + keep, "...", 3);
        dst[keep + 3] = '\0';
    } else {
        memcpy(dst, out, o + 1);
    }
}

static const char *str(const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : NULL;
}

static int boolean(const cJSON *obj, const char *key)
{
    return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(obj, key));
}

/* "HH:MM" exactly, else NULL. A trailing '*' (uncertainty marker) is ignored by the caller. */
static int hhmm(const char *s, int *mins)
{
    if (!s || strlen(s) != 5 || s[2] != ':') return 0;
    if (!isdigit((unsigned char)s[0]) || !isdigit((unsigned char)s[1]) ||
        !isdigit((unsigned char)s[3]) || !isdigit((unsigned char)s[4])) return 0;
    int h = (s[0] - '0') * 10 + (s[1] - '0'), m = (s[3] - '0') * 10 + (s[4] - '0');
    if (h > 23 || m > 59) return 0;
    if (mins) *mins = h * 60 + m;
    return 1;
}

/* ---- timestamps ----------------------------------------------------------------------------- */

static int64_t days_from_civil(int y, int m, int d)   /* Howard Hinnant's algorithm */
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/* "2026-10-02T15:17:47.6126798+01:00" -> Unix seconds. 0 if malformed or no offset. */
static int64_t parse_generated(const char *s)
{
    int y, mo, d, h, mi, sec, n = 0;
    if (!s || sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d%n", &y, &mo, &d, &h, &mi, &sec, &n) != 6) return 0;
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || sec > 60) return 0;
    const char *p = s + n;
    if (*p == '.') { p++; while (isdigit((unsigned char)*p)) p++; }
    int64_t off = 0;
    if (*p == 'Z') {
        off = 0;
    } else if (*p == '+' || *p == '-') {
        int oh, om;
        if (sscanf(p + 1, "%2d:%2d", &oh, &om) != 2) return 0;
        off = (int64_t)(oh * 3600 + om * 60) * (*p == '-' ? -1 : 1);
    } else {
        return 0;   /* no offset: not safe to guess London vs UTC */
    }
    return days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + sec - off;
}

/* ---- services ------------------------------------------------------------------------------- */

static const char *prev_lookup(const prev_platforms_t *prev, const char *id)
{
    if (!prev || !id) return NULL;
    for (int i = 0; i < prev->n; i++)
        if (!strcmp(prev->e[i].id, id)) return prev->e[i].plat;
    return NULL;
}

static int parse_service(const cJSON *raw, const prev_platforms_t *prev, const char *flt_name,
                         board_service_t *s, char id[24])
{
    memset(s, 0, sizeof *s);
    const char *std = str(raw, "std");
    int std_m;
    if (!hhmm(std, &std_m)) return 0;
    memcpy(s->std, std, 5);       /* hhmm() proved it is exactly "HH:MM" */
    s->std[5] = '\0';

    char etd_raw[16] = "";
    if (str(raw, "etd")) clean_copy(etd_raw, sizeof etd_raw, str(raw, "etd"), 12, 0);
    size_t el = strlen(etd_raw);
    if (el && etd_raw[el - 1] == '*') etd_raw[--el] = '\0';

    const char *why = NULL;
    int etd_m;
    if (boolean(raw, "isCancelled") || !strcmp(etd_raw, "Cancelled")) {
        s->state = SVC_CANCELLED;
        why = str(raw, "cancelReason");
    } else if (boolean(raw, "filterLocationCancelled")) {
        s->state = SVC_CANCELLED;   /* still runs, but no longer calls where we filter */
        char tmp[MAX_REASON + 1];
        if (flt_name && flt_name[0]) snprintf(tmp, sizeof tmp, "Not stopping at %s", flt_name);
        else snprintf(tmp, sizeof tmp, "Not stopping here");
        clean_copy(s->why, sizeof s->why, tmp, MAX_REASON, 0);
    } else if (!strcmp(etd_raw, "On time")) {
        /* Darwin can say this from the schedule alone; the board cannot tell us which. */
        s->state = SVC_ON_TIME;
        snprintf(s->etd, sizeof s->etd, "%s", s->std);
    } else if (!strcmp(etd_raw, "Delayed")) {
        s->state = SVC_DELAYED;
        why = str(raw, "delayReason");
    } else if (hhmm(etd_raw, &etd_m)) {
        memcpy(s->etd, etd_raw, 5);   /* hhmm() proved it is exactly "HH:MM" */
        s->etd[5] = '\0';
        int late = etd_m - std_m;
        if (late < -720) late += 1440;     /* across midnight */
        if (late < 0) late = 0;
        s->late = (uint8_t)(late > 255 ? 255 : late);
        s->state = late > 0 ? SVC_DELAYED : SVC_ON_TIME;
        if (late > 0) why = str(raw, "delayReason");
    } else {
        s->state = SVC_UNKNOWN;
    }
    if (why) clean_copy(s->why, sizeof s->why, why, MAX_REASON, 0);

    const char *plat = str(raw, "platform");
    if (plat) clean_copy(s->plat, sizeof s->plat, plat, sizeof s->plat - 1, 0);

    const cJSON *dests = cJSON_GetObjectItemCaseSensitive(raw, "destination");
    const cJSON *d0 = cJSON_IsArray(dests) ? cJSON_GetArrayItem(dests, 0) : NULL;
    const char *dn = d0 ? str(d0, "locationName") : NULL;
    clean_copy(s->dest, sizeof s->dest, dn ? dn : "Unknown", sizeof s->dest - 1, 0);
    if (!s->dest[0]) snprintf(s->dest, sizeof s->dest, "Unknown");

    id[0] = '\0';
    if (str(raw, "serviceID")) clean_copy(id, 24, str(raw, "serviceID"), 23, 0);
    const char *was = prev_lookup(prev, id);
    if (was && s->plat[0] && strcmp(was, s->plat)) snprintf(s->plat_was, sizeof s->plat_was, "%s", was);
    return 1;
}

ldb_result_t ldbws_parse(const char *json, size_t len, const char *label,
                         board_t *out, int64_t *generated, prev_platforms_t *prev)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return LDB_ERR_JSON;
    if (!cJSON_IsObject(root)) { cJSON_Delete(root); return LDB_ERR_SHAPE; }

    const cJSON *avail = cJSON_GetObjectItemCaseSensitive(root, "areServicesAvailable");
    if (cJSON_IsFalse(avail)) { cJSON_Delete(root); return LDB_UNAVAILABLE; }

    const char *loc = str(root, "locationName");
    if (!loc) { cJSON_Delete(root); return LDB_ERR_SHAPE; }

    board_t b;
    memset(&b, 0, sizeof b);
    b.mode = BOARD_LIVE;
    clean_copy(b.station, sizeof b.station, loc, sizeof b.station - 1, 0);
    snprintf(b.label, sizeof b.label, "%s", label ? label : "");

    const char *flt_name = str(root, "filterLocationName");
    prev_platforms_t next;
    memset(&next, 0, sizeof next);

    const cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "trainServices");
    if (cJSON_IsArray(list)) {
        const cJSON *raw;
        cJSON_ArrayForEach(raw, list) {
            if (b.count >= BOARD_MAX_SERVICES) break;
            if (!cJSON_IsObject(raw)) continue;
            char id[24];
            board_service_t *s = &b.svc[b.count];
            if (!parse_service(raw, prev, flt_name, s, id)) continue;
            if (id[0]) {
                snprintf(next.e[next.n].id, sizeof next.e[0].id, "%s", id);
                snprintf(next.e[next.n].plat, sizeof next.e[0].plat, "%s", s->plat);
                next.n++;
            }
            b.count++;
        }
    }

    const cJSON *msgs = cJSON_GetObjectItemCaseSensitive(root, "nrccMessages");
    if (cJSON_IsArray(msgs)) {
        const cJSON *m;
        cJSON_ArrayForEach(m, msgs) {
            const char *text = cJSON_IsString(m) ? m->valuestring : (cJSON_IsObject(m) ? str(m, "Value") : NULL);
            if (!text) continue;
            clean_copy(b.message, sizeof b.message, text, MAX_MESSAGE, 1);
            if (b.message[0]) break;
        }
    }

    if (generated) *generated = parse_generated(str(root, "generatedAt"));
    cJSON_Delete(root);

    *out = b;
    if (prev) *prev = next;
    return LDB_OK;
}
