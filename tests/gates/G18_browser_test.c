#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "browser.h"
#include "docsvc.h"
#include "sha256.h"

static int failures;
#define CHECK(c, name) do { if (c) printf("PASS %s\n", name); else { printf("FAIL %s\n", name); failures++; } } while (0)

/* ---------- fake transport (no sockets anywhere) ---------- */
typedef struct {
    char host[64], path[200];
    int status;
    char ctype[48];
    uint8_t body[4096];
    size_t blen;
    char extra[200];
    uint8_t raw[8192];
    size_t rawlen;
    int has_raw;
} route_t;

static route_t routes[24];
static unsigned nroutes, calls;
static int transport_fail, transport_oversize;
static char last_request[512];

static void reset_net(void)
{
    nroutes = 0; calls = 0; transport_fail = 0; transport_oversize = 0; last_request[0] = 0;
}

static route_t *add_route(const char *host, const char *path, int status, const char *ctype, const void *body, size_t blen)
{
    route_t *r = &routes[nroutes++];
    memset(r, 0, sizeof *r);
    snprintf(r->host, sizeof r->host, "%s", host);
    snprintf(r->path, sizeof r->path, "%s", path);
    r->status = status;
    snprintf(r->ctype, sizeof r->ctype, "%s", ctype);
    memcpy(r->body, body, blen);
    r->blen = blen;
    return r;
}

static route_t *add_html(const char *host, const char *path, const char *html)
{
    return add_route(host, path, 200, "text/html; charset=utf-8", html, strlen(html));
}

static void add_redirect(const char *host, const char *path, const char *location)
{
    route_t *r = add_route(host, path, 302, "text/html", "", 0);
    snprintf(r->extra, sizeof r->extra, "Location: %s\r\n", location);
}


static int transport(void *ctx, const os_url *u, const char *req, size_t rl, uint8_t *resp, size_t cap, size_t *outlen)
{
    (void)ctx;
    calls++;
    snprintf(last_request, sizeof last_request, "%.*s", (int)(rl < sizeof last_request - 1 ? rl : sizeof last_request - 1), req);
    if (transport_fail) return -1;
    for (unsigned i = 0; i < nroutes; i++) {
        route_t *r = &routes[i];
        size_t n;
        if (strcmp(r->host, u->host) || strcmp(r->path, u->path)) continue;
        if (r->has_raw) { memcpy(resp, r->raw, r->rawlen); *outlen = r->rawlen; return 0; }
        n = (size_t)snprintf((char *)resp, cap, "HTTP/1.1 %d X\r\nContent-Type: %s\r\nContent-Length: %zu\r\n%sConnection: close\r\n\r\n",
                             r->status, r->ctype, r->blen, r->extra);
        memcpy(resp + n, r->body, r->blen);
        *outlen = n + r->blen + (transport_oversize ? 100000u : 0u);
        return 0;
    }
    {
        const char *nf = "not found";
        size_t n = (size_t)snprintf((char *)resp, cap, "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: 9\r\n\r\n%s", nf);
        *outlen = n;
        return 0;
    }
}

/* ---------- helpers ---------- */
static os_url U;
static int url_ok(const char *t) { return os_url_parse(t, &U) == OS_NET_OK; }
static int url_is(const char *t, int https, const char *host, unsigned port, const char *path)
{
    return url_ok(t) && U.https == https && !strcmp(U.host, host) && U.port == port && !strcmp(U.path, path);
}
static int resolves_to(const char *base, const char *ref, const char *expect)
{
    os_url b, o;
    char f[300];
    if (os_url_parse(base, &b)) return 0;
    if (os_url_resolve(&b, ref, &o)) return 0;
    os_url_format(&o, f, sizeof f);
    return !strcmp(f, expect);
}
static int resolve_rc(const char *base, const char *ref)
{
    os_url b, o;
    os_url_parse(base, &b);
    return os_url_resolve(&b, ref, &o);
}

static os_html_doc doc;
static char page[8192];
static int render(const char *html, unsigned width)
{
    size_t n;
    int rc = os_html_parse(&doc, html, strlen(html));
    if (rc) return rc;
    return os_html_render(&doc, width, page, sizeof page, &n);
}
static int renders(const char *html, unsigned w, const char *expect)
{
    return render(html, w) == 0 && !strcmp(page, expect);
}

static os_js_result jr;
static int js(const char *src) { return os_js_run(src, strlen(src), OS_JS_FUEL_DEFAULT, &jr); }
static int js_out(const char *src, const char *expect) { return js(src) == OS_JS_OK && !strcmp(jr.output, expect); }

static os_browser br;
static os_image img;

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xFFFFu); put16(p + 2, v >> 16); }

/* 2x2 24-bit BMP: bottom row (stored first) = red, green ; top row = blue, white. */
static size_t make_bmp(uint8_t *b, int top_down)
{
    size_t off = 54, stride = 8, total = off + stride * 2;
    memset(b, 0, total);
    b[0] = 'B'; b[1] = 'M';
    put32(b + 2, (uint32_t)total); put32(b + 10, (uint32_t)off); put32(b + 14, 40);
    put32(b + 18, 2); put32(b + 22, top_down ? (uint32_t)-2 : 2u);
    put16(b + 26, 1); put16(b + 28, 24);
    {
        static const uint8_t bottom[6] = { 0, 0, 255, 0, 255, 0 }; /* BGR: red, green */
        static const uint8_t top[6] = { 255, 0, 0, 255, 255, 255 };  /* blue, white */
        memcpy(b + off + (top_down ? stride : 0), bottom, 6);
        memcpy(b + off + (top_down ? 0 : stride), top, 6);
    }
    return total;
}

static void test_urls(void)
{
    char big[400], f[64];
    CHECK(url_is("http://Example.COM:8080/a/b?x=1#frag", 0, "example.com", 8080, "/a/b?x=1"), "G18-URL-PARSE-NORMALIZES-HOST-PORT-QUERY-AND-DROPS-FRAGMENT");
    CHECK(url_is("http://a.b", 0, "a.b", 80, "/") && url_is("https://a.b/x", 1, "a.b", 443, "/x") && url_is("HTTP://a.b?q=1", 0, "a.b", 80, "/?q=1"),
          "G18-URL-DEFAULT-PORTS-AND-IMPLIED-ROOT");
    memset(big, 'a', sizeof big);
    memcpy(big, "http://h/", 9);
    big[300] = 0;
    CHECK(!url_ok("ftp://x/") && !url_ok("file:///etc/passwd") && !url_ok("javascript:alert(1)") && !url_ok("//x/y") && !url_ok("x") &&
          !url_ok("") && !url_ok("http://") && !url_ok("http:///x") && !url_ok(big) && os_url_parse(0, &U) == OS_NET_ERR_ARGUMENT &&
          os_url_parse("http://h/", 0) == OS_NET_ERR_ARGUMENT, "G18-URL-REJECTS-UNSUPPORTED-SCHEMES-AND-EMPTY-INPUT");
    CHECK(!url_ok("http://user@host/") && !url_ok("http://user:pw@host/") && !url_ok("http://host:0/") && !url_ok("http://host:65536/") &&
          !url_ok("http://host:/") && !url_ok("http://host:80x/") && !url_ok("http://[::1]/") && !url_ok("http://a..b/") && !url_ok("http://-a/") &&
          !url_ok("http://a-/") && !url_ok(".a.b") && !url_ok("http://a.b./") && !url_ok("http://ho st/") && !url_ok("http://h/p q") && !url_ok("http://h/\\x") &&
          !url_ok("http://h/%00") && !url_ok("http://h/a\"b") && !url_ok("http://h/<s>") && !url_ok("http://h/\x01"), "G18-URL-REJECTS-MALFORMED-AUTHORITY-AND-CHARACTERS");
    CHECK(url_is("http://h/a/b/../c/./d", 0, "h", 80, "/a/c/d") && url_is("http://h/../../x", 0, "h", 80, "/x") && url_is("http://h/a/b/..", 0, "h", 80, "/a/") &&
          url_is("http://h/a//b", 0, "h", 80, "/a/b") && url_is("http://h/a/./", 0, "h", 80, "/a/") && url_is("http://h/a/../b?x=../y", 0, "h", 80, "/b?x=../y"),
          "G18-URL-DOT-SEGMENTS-NORMALIZED-AND-CANNOT-ESCAPE-ROOT");
    CHECK(resolves_to("http://h/a/b/c.html", "d.html", "http://h/a/b/d.html") && resolves_to("http://h/a/b/c.html", "../x", "http://h/a/x") &&
          resolves_to("http://h/a/b/c.html", "/r", "http://h/r") && resolves_to("http://h/a/b/c.html", "?q=1", "http://h/a/b/c.html?q=1") &&
          resolves_to("http://h/a/b/c.html", "#f", "http://h/a/b/c.html") && resolves_to("http://h/a/b/c.html", "", "http://h/a/b/c.html") &&
          resolves_to("http://h/a/b/c.html", "http://o:81/z", "http://o:81/z") && resolves_to("http://h/a/b/c.html", "../../../../x", "http://h/x") &&
          resolves_to("http://h:81/a/", "x", "http://h:81/a/x"), "G18-URL-RESOLVE-RELATIVE-REFERENCES");
    CHECK(resolve_rc("http://h/", "//evil/x") == OS_NET_ERR_UNSUPPORTED && resolve_rc("http://h/", "javascript:alert(1)") == OS_NET_ERR_SCHEME &&
          resolve_rc("http://h/", "mailto:a@b") == OS_NET_ERR_SCHEME && resolve_rc("http://h/", "data:text/html,x") == OS_NET_ERR_SCHEME &&
          resolve_rc("http://h/", "ftp://x/") == OS_NET_ERR_SCHEME && resolve_rc("http://h/", "a b") == OS_NET_ERR_URL, "G18-URL-RESOLVE-REFUSES-UNSAFE-REFERENCES");
    os_url_parse("http://h:8080/p?q", &U);
    CHECK(os_url_format(&U, f, sizeof f) == 0 && !strcmp(f, "http://h:8080/p?q") && os_url_format(&U, f, 8) == OS_NET_ERR_SIZE, "G18-URL-FORMAT-ROUNDTRIP-AND-BOUNDS");
}

static void test_http(void)
{
    char req[512], big[3000];
    os_http_response r;
    int n;
    const char *ok = "HTTP/1.1 200 OK\r\nContent-Type: Text/HTML; charset=utf-8\r\nContent-Length: 5\r\n\r\nhello";
    os_url_parse("http://h:8080/p?x=1", &U);
    n = os_http_build_request(&U, req, sizeof req);
    CHECK(n > 0 && !strncmp(req, "GET /p?x=1 HTTP/1.1\r\nHost: h:8080\r\n", 34) && strstr(req, "Connection: close\r\n\r\n") && !strstr(req, "Cookie") &&
          !strstr(req, "Authorization"), "G18-HTTP-REQUEST-IS-GET-WITH-NO-CREDENTIALS");
    CHECK(os_http_build_request(&U, req, 20) == OS_NET_ERR_SIZE, "G18-HTTP-REQUEST-BOUNDED");
    CHECK(os_http_parse_response((const uint8_t *)ok, strlen(ok), &r) == 0 && r.status == 200 && !strcmp(r.content_type, "text/html") && r.body_len == 5 &&
          !memcmp(ok + r.body_off, "hello", 5), "G18-HTTP-PARSE-OK-LOWERCASES-TYPE-AND-LOCATES-BODY");
    {
        const char *nocl = "HTTP/1.0 204 No Content\r\nX: y\r\n\r\nrest";
        CHECK(os_http_parse_response((const uint8_t *)nocl, strlen(nocl), &r) == 0 && r.status == 204 && r.body_len == 4, "G18-HTTP-NO-LENGTH-USES-CONNECTION-CLOSE-BODY");
    }
#define BAD(s, code) (os_http_parse_response((const uint8_t *)(s), strlen(s), &r) == (code))
    CHECK(BAD("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n", OS_NET_ERR_UNSUPPORTED) &&
          BAD("HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: 0\r\n\r\n", OS_NET_ERR_UNSUPPORTED), "G18-HTTP-CHUNKED-AND-COMPRESSED-REFUSED");
    CHECK(BAD("HTTP/1.1 200 OK\nContent-Length: 0\n\n", OS_NET_ERR_RESPONSE) && BAD("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n", OS_NET_ERR_RESPONSE) &&
          BAD("HTTP/1.1 200 OK\r\nContent-Length: 0\r\nBad\x01: x\r\n\r\n", OS_NET_ERR_RESPONSE) && BAD("HTTP/1.1 200 OK\r\nNoColon\r\n\r\n", OS_NET_ERR_RESPONSE),
          "G18-HTTP-MALFORMED-FRAMING-REFUSED");
    CHECK(BAD("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nshort", OS_NET_ERR_RESPONSE) && BAD("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nlonger", OS_NET_ERR_RESPONSE),
          "G18-HTTP-LENGTH-MUST-MATCH-BODY-EXACTLY");
    CHECK(BAD("HTTP/1.1 200 OK\r\nContent-Length: 3\r\nContent-Length: 4\r\n\r\nabc", OS_NET_ERR_RESPONSE) &&
          BAD("HTTP/1.1 200 OK\r\nContent-Length: 3\r\nContent-Length: 3\r\n\r\nabc", OS_NET_OK) && BAD("HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n", OS_NET_ERR_RESPONSE) &&
          BAD("HTTP/1.1 200 OK\r\nContent-Length: 1x\r\n\r\nx", OS_NET_ERR_RESPONSE) && BAD("HTTP/1.1 200 OK\r\nContent-Length: 99999999999\r\n\r\n", OS_NET_ERR_RESPONSE),
          "G18-HTTP-CONFLICTING-OR-INVALID-LENGTH-REFUSED");
    CHECK(BAD("HTTP/2 200 OK\r\n\r\nxxxxxxxxxxxxxxxx", OS_NET_ERR_RESPONSE) && BAD("HTTP/1.1 99 X\r\n\r\nxxxxxxxxxxxxxxxxx", OS_NET_ERR_RESPONSE) &&
          BAD("HTTP/1.1 600 X\r\n\r\nxxxxxxxxxxxxxxxxx", OS_NET_ERR_RESPONSE) && BAD("HTTP/1.1 2x0 X\r\n\r\nxxxxxxxxxxxxxxxxx", OS_NET_ERR_RESPONSE) &&
          BAD("short", OS_NET_ERR_RESPONSE), "G18-HTTP-BAD-STATUS-LINE-REFUSED");
    memset(big, 0, sizeof big);
    strcpy(big, "HTTP/1.1 200 OK\r\nX-Pad: ");
    memset(big + strlen(big), 'a', 2500);
    strcat(big, "\r\n\r\n");
    CHECK(BAD(big, OS_NET_ERR_RESPONSE), "G18-HTTP-HEADER-SIZE-BOUNDED");
    CHECK(BAD("HTTP/1.1 200 OK\r\nContent-Type: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\r\nContent-Length: 0\r\n\r\n", OS_NET_ERR_RESPONSE),
          "G18-HTTP-OVERLONG-HEADER-VALUE-REFUSED");
#undef BAD
}

static void test_net_policy(void)
{
    os_net_policy pol;
    os_http_response r;
    uint8_t buf[2048];
    os_url u;
    reset_net();
    add_html("a.test", "/", "<p>a</p>");
    add_html("b.test", "/", "<p>b</p>");
    os_net_policy_init(&pol, 1024);
    os_url_parse("http://a.test/", &u);
    CHECK(os_net_fetch(&pol, transport, 0, &u, buf, sizeof buf, &r) == OS_NET_ERR_DENIED && calls == 0, "G18-NET-DENIES-EVERYTHING-BY-DEFAULT");
    os_net_policy_allow(&pol, "A.Test");
    CHECK(os_net_fetch(&pol, transport, 0, &u, buf, sizeof buf, &r) == OS_NET_OK && r.status == 200 && calls == 1, "G18-NET-ALLOWED-HOST-FETCHES");
    os_url_parse("http://b.test/", &u);
    CHECK(os_net_fetch(&pol, transport, 0, &u, buf, sizeof buf, &r) == OS_NET_ERR_DENIED && calls == 1, "G18-NET-NON-ALLOWED-HOST-NEVER-REACHES-TRANSPORT");
    CHECK(os_net_policy_allow(&pol, "bad host") != 0 && os_net_policy_allow(&pol, "c.test") == 0 && os_net_policy_allow(&pol, "d.test") == 0 &&
          os_net_policy_allow(&pol, "e.test") == 0 && os_net_policy_allow(&pol, "f.test") != 0, "G18-NET-ALLOWLIST-VALIDATED-AND-BOUNDED");
    os_net_policy_init(&pol, 1024);
    os_net_policy_allow(&pol, "a.test");
    os_url_parse("https://a.test/", &u);
    calls = 0;
    CHECK(os_net_fetch(&pol, transport, 0, &u, buf, sizeof buf, &r) == OS_NET_ERR_TLS && calls == 0, "G18-NET-HTTPS-REFUSED-WITHOUT-TLS");
    add_redirect("a.test", "/r1", "/");
    os_url_parse("http://a.test/r1", &u);
    CHECK(os_net_fetch(&pol, transport, 0, &u, buf, sizeof buf, &r) == OS_NET_OK && r.status == 200 && !strcmp(u.path, "/"), "G18-NET-RELATIVE-REDIRECT-FOLLOWED-AND-URL-UPDATED");
    add_redirect("a.test", "/r2", "http://b.test/");
    os_url_parse("http://a.test/r2", &u);
    calls = 0;
    CHECK(os_net_fetch(&pol, transport, 0, &u, buf, sizeof buf, &r) == OS_NET_ERR_DENIED && calls == 1, "G18-NET-REDIRECT-TO-DISALLOWED-HOST-DENIED");
    add_redirect("a.test", "/r3", "https://a.test/");
    os_url_parse("http://a.test/r3", &u);
    CHECK(os_net_fetch(&pol, transport, 0, &u, buf, sizeof buf, &r) == OS_NET_ERR_TLS, "G18-NET-REDIRECT-DOWNGRADE-TO-UNSUPPORTED-TLS-REFUSED");
    add_redirect("a.test", "/l1", "/l2"); add_redirect("a.test", "/l2", "/l3"); add_redirect("a.test", "/l3", "/l4"); add_redirect("a.test", "/l4", "/l5"); add_redirect("a.test", "/l5", "/");
    os_url_parse("http://a.test/l1", &u);
    CHECK(os_net_fetch(&pol, transport, 0, &u, buf, sizeof buf, &r) == OS_NET_ERR_REDIRECTS, "G18-NET-REDIRECT-CHAIN-BOUNDED");
    add_redirect("a.test", "/r4", "javascript:alert(1)");
    os_url_parse("http://a.test/r4", &u);
    CHECK(os_net_fetch(&pol, transport, 0, &u, buf, sizeof buf, &r) == OS_NET_ERR_SCHEME, "G18-NET-REDIRECT-TO-UNSAFE-SCHEME-REFUSED");
    os_url_parse("http://a.test/", &u);
    transport_oversize = 1;
    CHECK(os_net_fetch(&pol, transport, 0, &u, buf, sizeof buf, &r) == OS_NET_ERR_SIZE, "G18-NET-RESPONSE-SIZE-BOUNDED");
    transport_oversize = 0; transport_fail = 1;
    CHECK(os_net_fetch(&pol, transport, 0, &u, buf, sizeof buf, &r) == OS_NET_ERR_TRANSPORT, "G18-NET-TRANSPORT-FAILURE-REPORTED");
    CHECK(os_net_fetch(0, transport, 0, &u, buf, sizeof buf, &r) == OS_NET_ERR_ARGUMENT && os_net_fetch(&pol, 0, 0, &u, buf, sizeof buf, &r) == OS_NET_ERR_ARGUMENT,
          "G18-NET-ARGUMENTS-VALIDATED");
}

static void test_html(void)
{
    char href[300], out[16];
    CHECK(renders("<html><head><title>T</title></head><body><h1>Hello</h1><p>One two <b>three</b> four.</p></body></html>", 60, "HELLO\n\nOne two three four."),
          "G18-HTML-BASIC-STRUCTURE-RENDERS");
    CHECK(doc.title_len == 1 && !strcmp(doc.pool + doc.title_off, "T"), "G18-HTML-TITLE-CAPTURED-AND-NOT-RENDERED");
    CHECK(renders("<p>a &amp; b &lt;c&gt; &quot;d&quot; &#65;&#x42; x&nbsp;y &bogus; &#1;</p>", 60, "a & b <c> \"d\" AB x y &bogus; ?"), "G18-HTML-ENTITIES-DECODED-UNKNOWN-KEPT-CONTROL-REPLACED");
    CHECK(renders("<p>a   \n\t b</p>\n\n   <p>  c  </p>", 60, "a b\n\nc"), "G18-HTML-WHITESPACE-COLLAPSED");
    CHECK(renders("<p>x<b>y</b> <i>z</i>w</p>", 60, "xy zw"), "G18-HTML-INLINE-ADJACENCY-AND-SPACES-PRESERVED");
    CHECK(renders("<ul><li>one</li><li>two</li></ul>", 60, "*one\n*two") || renders("<ul><li>one</li><li>two</li></ul>", 60, "* one\n* two"), "G18-HTML-LISTS-RENDERED");
    CHECK(renders("<pre>a  b\n  c</pre>", 60, "a  b\n  c"), "G18-HTML-PRE-PRESERVES-WHITESPACE");
    CHECK(renders("a<br>b<hr>c", 60, "a\nb\n----------------------------------------\nc"), "G18-HTML-BREAK-AND-RULE");
    {
        const char *t = "<p>aaaa bbbb cccc dddd eeee ffff gggg hhhh</p>";
        int ok = render(t, 20) == 0, longest = 0, cur = 0;
        for (char *p = page;; p++) { if (*p == '\n' || !*p) { if (cur > longest) longest = cur; cur = 0; if (!*p) break; } else cur++; }
        CHECK(ok && longest <= 20 && strchr(page, '\n'), "G18-HTML-WORD-WRAP-RESPECTS-WIDTH");
        ok = render("<p>abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz</p>", 20) == 0; longest = 0; cur = 0;
        for (char *p = page;; p++) { if (*p == '\n' || !*p) { if (cur > longest) longest = cur; cur = 0; if (!*p) break; } else cur++; }
        CHECK(ok && longest == 20, "G18-HTML-OVERLONG-WORD-HARD-WRAPPED");
    }
    CHECK(render("<p>see <a href=\"/x.html\">one</a> and <a href='http://o.test/y'>two</a> and <a>none</a> <a href=z>three</a></p>", 60) == 0 &&
          doc.link_count == 3 && strstr(page, "one[1]") && strstr(page, "two[2]") && strstr(page, "three[3]") && !strstr(page, "none[") &&
          os_html_link_href(&doc, 1, href, sizeof href) == 7 && !strcmp(href, "/x.html") && os_html_link_href(&doc, 3, href, sizeof href) == 1 &&
          os_html_link_href(&doc, 4, href, sizeof href) < 0 && os_html_link_href(&doc, 0, href, sizeof href) < 0 && os_html_link_href(&doc, 1, href, 3) < 0,
          "G18-HTML-LINKS-NUMBERED-AND-RESOLVABLE");
    {
        char many[2000] = "";
        for (int i = 0; i < 20; i++) strcat(many, "<a href=/l>x</a> ");
        CHECK(render(many, 60) == 0 && doc.link_count == OS_HTML_MAX_LINKS && strstr(page, "[16]") && !strstr(page, "[17]"), "G18-HTML-LINK-COUNT-BOUNDED");
    }
    CHECK(render("<p>a<img src=\"/p.bmp\">b<input name=q>c<img src=\"/q.bmp\"></p>", 60) == 0 && doc.image_count == 2 && strstr(page, "[img1]") && strstr(page, "[img2]") &&
          strstr(page, "[input]") && os_html_image_src(&doc, 2, href, sizeof href) == 6 && !strcmp(href, "/q.bmp") && os_html_image_src(&doc, 3, href, sizeof href) < 0,
          "G18-HTML-IMAGES-AND-INPUTS-RENDERED-AS-PLACEHOLDERS");
    CHECK(render("<head><style>p{color:red}</style></head><body><script>print(1);</script><p>x</p><script>print(2);</script></body>", 60) == 0 && !strcmp(page, "x") &&
          doc.script_count == 2 && !strcmp(doc.pool + doc.nodes[doc.script_node[0]].text_off, "print(1);"), "G18-HTML-SCRIPT-COLLECTED-STYLE-DROPPED-NEITHER-RENDERED");
    CHECK(render("<p onclick=\"evil()\" onmouseover='x()'>a</p><a href=/x onfocus=y>b</a>", 60) == 0 && doc.script_count == 0 && doc.ignored_handlers == 3,
          "G18-HTML-INLINE-HANDLERS-COUNTED-AND-NEVER-COLLECTED");
    CHECK(renders("<!DOCTYPE html><!-- c --><p>a<!-- b -->c</p>", 60, "ac"), "G18-HTML-COMMENTS-AND-DOCTYPE-SKIPPED");
    CHECK(renders("<p>a <blink>b <marquee>c</marquee></blink> d</p>", 60, "a b c d"), "G18-HTML-UNKNOWN-TAGS-ARE-TRANSPARENT");
    CHECK(renders("<p><b>a</i>b</p></div>c", 60, "ab\n\nc") || renders("<p><b>a</i>b</p></div>c", 60, "ab\n\nc"), "G18-HTML-MISNESTED-AND-STRAY-END-TAGS-TOLERATED");
    CHECK(renders("1 < 2 and 3 <4", 60, "1 < 2 and 3 <4") || render("1 < 2 and 3 <4", 60) == 0, "G18-HTML-LITERAL-LESS-THAN-IS-TEXT");
    {
        static char big[9000];
        char nodes[3000] = "";
        char deep[400] = "";
        memset(big, 'a', sizeof big - 1); big[sizeof big - 1] = 0;
        CHECK(os_html_parse(&doc, big, 8193) == OS_HTML_ERR_SIZE && doc.count == 0, "G18-HTML-INPUT-SIZE-BOUNDED");
        for (int i = 0; i < 100; i++) strcat(nodes, "<p>x</p>");
        CHECK(os_html_parse(&doc, nodes, strlen(nodes)) == OS_HTML_ERR_LIMIT && doc.count == 0 && doc.link_count == 0, "G18-HTML-NODE-COUNT-BOUNDED-AND-FAILS-CLOSED");
        for (int i = 0; i < 20; i++) strcat(deep, "<div>");
        CHECK(os_html_parse(&doc, deep, strlen(deep)) == OS_HTML_ERR_DEPTH, "G18-HTML-NESTING-DEPTH-BOUNDED");
    }
    CHECK(render("<!-- never closed", 60) == OS_HTML_ERR_SYNTAX && render("<p class=\"x", 60) == OS_HTML_ERR_SYNTAX && render("<script>print(1);", 60) == OS_HTML_ERR_SYNTAX &&
          render("<a href=", 60) == OS_HTML_ERR_SYNTAX && render("<!doctype", 60) == OS_HTML_ERR_SYNTAX, "G18-HTML-UNTERMINATED-CONSTRUCTS-FAIL-CLOSED");
    CHECK(render("<script>1</script><script>2</script><script>3</script><script>4</script><script>5</script>", 60) == OS_HTML_ERR_LIMIT, "G18-HTML-SCRIPT-COUNT-BOUNDED");
    {
        char s[1400] = "<script>";
        memset(s + 8, 'x', 1100);
        strcpy(s + 8 + 1100, "</script>");
        CHECK(render(s, 60) == OS_HTML_ERR_LIMIT, "G18-HTML-SCRIPT-SIZE-BOUNDED");
    }
    {
        char a[400] = "<a href=\"";
        memset(a + 9, 'h', 250);
        strcpy(a + 9 + 250, "\">x</a>");
        CHECK(render(a, 60) == 0 && doc.link_count == 0 && !strstr(page, "[1]"), "G18-HTML-OVERLONG-HREF-DROPPED-NOT-TRUNCATED");
        CHECK(render("<a href=\"/a\x01\x7f\">x</a>", 60) == 0 && doc.link_count == 0, "G18-HTML-CONTROL-BYTES-IN-HREF-DROPPED");
    }
    {
        size_t n = 0;
        render("<p>0123456789 0123456789 0123456789 0123456789</p>", 20);
        CHECK(os_html_render(&doc, 20, out, sizeof out, &n) == OS_HTML_ERR_RENDER && out[sizeof out - 1] == 0 && strlen(out) < sizeof out && os_html_render(&doc, 10, out, sizeof out, &n) == OS_HTML_ERR_ARGUMENT &&
              os_html_render(&doc, 200, out, sizeof out, &n) == OS_HTML_ERR_ARGUMENT, "G18-HTML-RENDER-OUTPUT-BOUNDED-AND-ARGUMENTS-VALIDATED");
    }
    CHECK(render("<p>caf\xc3\xa9</p>", 60) == 0 && !strchr(page, '\xc3'), "G18-HTML-NON-ASCII-BYTES-NEUTRALIZED");
}

static void test_js(void)
{
    CHECK(js_out("print(1 + 2 * 3); print((1 + 2) * 3); print(10 - 4 - 3); print(7 / 2); print(7 % 4); print(-5 + 2);", "7\n9\n3\n3\n3\n-3\n"), "G18-JS-ARITHMETIC-AND-PRECEDENCE");
    CHECK(js_out("var s = 0; var i = 1; while (i < 101) { s = s + i; i = i + 1; } print(s);", "5050\n"), "G18-JS-WHILE-LOOP-COMPUTES-SUM");
    CHECK(js_out("let a = \"x\"; var b = a + 1 + \"y\"; print(b); document.write('ok'); print(1 + \"2\");", "x1y\nok\n12\n"), "G18-JS-STRINGS-AND-CONCATENATION");
    CHECK(js_out("print(true); print(false || true); print(!0); print(1 < 2); print(2 <= 1); print(3 > 2); print(3 >= 4); print(1 == 1); print(1 != 1); print('a' == 'a'); print('a' == 'b'); print(1 == '1'); print(1 === 1);",
                 "true\ntrue\ntrue\ntrue\nfalse\ntrue\nfalse\ntrue\nfalse\ntrue\nfalse\nfalse\ntrue\n"), "G18-JS-BOOLEANS-AND-COMPARISONS");
    CHECK(js_out("var x = 5; if (x > 3) print('big'); else print('small'); if (x > 9) { print('a'); } else { if (x == 5) print('five'); }", "big\nfive\n"), "G18-JS-IF-ELSE-AND-BLOCKS");
    CHECK(js_out("// c\nprint(1); /* multi\nline */ print(2);", "1\n2\n"), "G18-JS-COMMENTS");
    CHECK(js_out("print(2147483647 + 1); print(0 - 2147483647 - 2);", "-2147483648\n2147483647\n"), "G18-JS-INT32-WRAPS-WITHOUT-UB");
    CHECK(js("print(1 / 0);") == OS_JS_ERR_RUNTIME && js("print(1 % 0);") == OS_JS_ERR_RUNTIME && js("print((0 - 2147483647 - 1) / (0 - 1));") == OS_JS_ERR_RUNTIME && jr.output_len == 0,
          "G18-JS-DIVISION-TRAPS");
    CHECK(js("while (true) { }") == OS_JS_ERR_FUEL && js("var i = 0; while (i < 1) { i = 0; }") == OS_JS_ERR_FUEL && jr.steps > 0, "G18-JS-FUEL-BOUNDS-INFINITE-LOOPS");
    CHECK(os_js_run("while (true) {}", 15, 0, &jr) == OS_JS_ERR_ARGUMENT && os_js_run(0, 0, 10, &jr) == OS_JS_ERR_ARGUMENT && os_js_run("1", 1, 10, 0) == OS_JS_ERR_ARGUMENT, "G18-JS-ARGUMENTS-VALIDATED");
    CHECK(js("print(zz);") == OS_JS_ERR_RUNTIME && js("zz = 1;") == OS_JS_ERR_RUNTIME, "G18-JS-UNDEFINED-AND-UNDECLARED-REFUSED");
    {
        const char *bad[] = { "function f() { return 1; }", "document.cookie = 'a=b';", "eval('1');", "var o = new Object();", "var f = 1.5;", "var y = x.y;",
                              "fetch('http://evil/');", "window.location = 'http://x/';", "import x;", "var a = [1,2];", "var o = {};", "setTimeout(1);",
                              "XMLHttpRequest();", "print(1)", "var = 1;", "if 1 print(1);", "else print(1);", "print('x' - 1);", "print(-'x');" };
        int all = 1;
        for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
            int rc = js(bad[i]);
            if (rc == OS_JS_OK || jr.output_len != 0) { all = 0; printf("  unexpectedly accepted: %s\n", bad[i]); }
        }
        CHECK(all, "G18-JS-UNSUPPORTED-AND-DANGEROUS-CONSTRUCTS-REJECTED-WITHOUT-EFFECT");
    }
    CHECK(js("print('abc);") == OS_JS_ERR_SYNTAX && js("/* open") == OS_JS_ERR_SYNTAX && js("{ print(1);") == OS_JS_ERR_SYNTAX && js("var a = 99999999999;") == OS_JS_ERR_LIMIT,
          "G18-JS-UNTERMINATED-CONSTRUCTS-AND-OVERSIZE-NUMBERS-REFUSED");
    CHECK(js("var i = 0; while (i < 1000) { print('xxxxxxxxxx'); i = i + 1; }") == OS_JS_ERR_OUTPUT && jr.output_len <= OS_JS_OUTPUT_MAX, "G18-JS-OUTPUT-BOUNDED");
    CHECK(js("var s = 'abcdefghij'; var i = 0; while (i < 1000) { s = s + s; i = i + 1; }") == OS_JS_ERR_LIMIT, "G18-JS-STRING-POOL-BOUNDED");
    CHECK(js("var a=1;var b=2;var c=3;var d=4;var e=5;var f=6;var g=7;var h=8;var i=9;var j=10;var k=11;var l=12;var m=13;") == OS_JS_ERR_LIMIT, "G18-JS-VARIABLE-COUNT-BOUNDED");
    CHECK(js("var a = ((((((((((((((((1)))))))))))))))); ") == OS_JS_ERR_LIMIT && js("{{{{{{{{{{{{{{{ print(1); }}}}}}}}}}}}}}}") == OS_JS_ERR_LIMIT &&
          js("var averyveryverylongidentifier = 1;") == OS_JS_ERR_LIMIT, "G18-JS-NESTING-AND-IDENTIFIER-LENGTH-BOUNDED");
    CHECK(js_out("if (false) { print(1 / 0); zz = 1; print(qq); var big = 1 / 0; } print('after');", "after\n") &&
          js_out("var i = 0; while (false) { i = 1 / 0; } print(i);", "0\n"), "G18-JS-SKIPPED-BRANCHES-HAVE-NO-SIDE-EFFECTS-OR-TRAPS");
    CHECK(js_out("var d = 0; if (d != 0 && 10 / d > 1) print('no'); else print('safe'); if (1 || 1 / 0) print('short');", "safe\nshort\n"), "G18-JS-SHORT-CIRCUIT-EVALUATION");
    CHECK(js("print(1); print(1 / 0); print(2);") == OS_JS_ERR_RUNTIME && !strcmp(jr.output, "1\n") && jr.error[0], "G18-JS-ERROR-KEEPS-PRIOR-OUTPUT-AND-REPORTS-MESSAGE");
}

static void test_images(void)
{
    uint8_t b[128], ppm_big[200];
    const uint8_t *p;
    char ascii[2000];
    size_t n = 0;
    {
        uint8_t ppm[64];
        int len = snprintf((char *)ppm, sizeof ppm, "P6\n# comment\n2 2\n255\n");
        memcpy(ppm + len, "\xff\x00\x00\x00\xff\x00\x00\x00\xff\xff\xff\xff", 12);
        CHECK(os_image_decode(ppm, (size_t)len + 12, &img) == 0 && img.width == 2 && img.height == 2 && img.rgb[0] == 255 && img.rgb[4] == 255 && img.rgb[8] == 255 && img.rgb[9] == 255,
              "G18-IMG-PPM-DECODED");
        CHECK(os_image_decode(ppm, (size_t)len + 11, &img) == OS_IMG_ERR_TRUNCATED && os_image_decode(ppm, (size_t)len + 13, &img) == OS_IMG_ERR_FORMAT, "G18-IMG-PPM-LENGTH-MUST-BE-EXACT");
        memcpy(ppm, "P6\n2 2\n65535\n", 13);
        CHECK(os_image_decode(ppm, 25, &img) == OS_IMG_ERR_FORMAT, "G18-IMG-PPM-NON-8-BIT-REFUSED");
    }
    memcpy(ppm_big, "P6\n129 1\n255\n", 13);
    CHECK(os_image_decode(ppm_big, 13, &img) == OS_IMG_ERR_SIZE, "G18-IMG-OVERSIZE-DIMENSIONS-REFUSED-BEFORE-ALLOCATION");
    memcpy(ppm_big, "P6\n0 1\n255\n", 11);
    CHECK(os_image_decode(ppm_big, 11, &img) == OS_IMG_ERR_SIZE && os_image_decode((const uint8_t *)"P6\n99999999999 1\n255\n", 20, &img) == OS_IMG_ERR_FORMAT, "G18-IMG-ZERO-AND-ABSURD-DIMENSIONS-REFUSED");
    n = make_bmp(b, 0);
    p = img.rgb;
    CHECK(os_image_decode(b, n, &img) == 0 && img.width == 2 && img.height == 2 && p[0] == 0 && p[1] == 0 && p[2] == 255 && p[3] == 255 && p[4] == 255 && p[5] == 255 &&
          p[6] == 255 && p[7] == 0 && p[8] == 0 && p[9] == 0 && p[10] == 255 && p[11] == 0, "G18-IMG-BMP-BOTTOM-UP-DECODED-WITH-PADDING");
    n = make_bmp(b, 1);
    CHECK(os_image_decode(b, n, &img) == 0 && img.rgb[2] == 255 && img.rgb[6] == 255, "G18-IMG-BMP-TOP-DOWN-DECODED");
    n = make_bmp(b, 0);
    {
        uint8_t m[128];
        memcpy(m, b, n); put16(m + 28, 8);
        CHECK(os_image_decode(m, n, &img) == OS_IMG_ERR_UNSUPPORTED, "G18-IMG-BMP-NON-24-BIT-REFUSED");
        memcpy(m, b, n); put32(m + 30, 1);
        CHECK(os_image_decode(m, n, &img) == OS_IMG_ERR_UNSUPPORTED, "G18-IMG-BMP-COMPRESSION-REFUSED");
        memcpy(m, b, n); put32(m + 2, (uint32_t)n + 1u);
        CHECK(os_image_decode(m, n, &img) == OS_IMG_ERR_FORMAT, "G18-IMG-BMP-FILE-SIZE-MUST-MATCH");
        memcpy(m, b, n); put32(m + 10, 4000);
        CHECK(os_image_decode(m, n, &img) == OS_IMG_ERR_TRUNCATED && os_image_decode(m, 20, &img) == OS_IMG_ERR_TRUNCATED, "G18-IMG-BMP-PIXEL-OFFSET-BOUNDED");
        memcpy(m, b, n); put32(m + 18, 100000);
        CHECK(os_image_decode(m, n, &img) == OS_IMG_ERR_SIZE, "G18-IMG-BMP-HUGE-WIDTH-REFUSED");
        memcpy(m, b, n); put32(m + 22, 0x80000000u);
        CHECK(os_image_decode(m, n, &img) == OS_IMG_ERR_SIZE, "G18-IMG-BMP-INT-MIN-HEIGHT-REFUSED");
        memcpy(m, b, n); put32(m + 14, 124);
        CHECK(os_image_decode(m, n, &img) == OS_IMG_ERR_UNSUPPORTED, "G18-IMG-BMP-NON-INFOHEADER-REFUSED");
    }
    CHECK(os_image_decode((const uint8_t *)"\x89PNG\r\n\x1a\n....", 12, &img) == OS_IMG_ERR_UNSUPPORTED && os_image_decode((const uint8_t *)"\xff\xd8\xff\xe0xxxx", 8, &img) == OS_IMG_ERR_UNSUPPORTED &&
          os_image_decode((const uint8_t *)"GIF89a....", 10, &img) == OS_IMG_ERR_UNSUPPORTED && os_image_decode((const uint8_t *)"garbage", 7, &img) == OS_IMG_ERR_FORMAT &&
          os_image_decode((const uint8_t *)"x", 1, &img) == OS_IMG_ERR_FORMAT && os_image_decode(0, 0, &img) == OS_IMG_ERR_ARGUMENT, "G18-IMG-PNG-JPEG-GIF-AND-UNKNOWN-REFUSED-NO-DECOMPRESSOR");
    n = make_bmp(b, 0);
    os_image_decode(b, n, &img);
    CHECK(os_image_ascii(&img, 4, ascii, sizeof ascii, &n) == 0 && n > 0 && ascii[n - 1] == '\n' && os_image_ascii(&img, 4, ascii, 3, &n) == OS_IMG_ERR_SIZE_OUT &&
          os_image_ascii(&img, 2, ascii, sizeof ascii, &n) == OS_IMG_ERR_ARGUMENT && os_image_ascii(&img, 81, ascii, sizeof ascii, &n) == OS_IMG_ERR_ARGUMENT, "G18-IMG-ASCII-PRESENTATION-BOUNDED");
    {
        os_image white;
        white.width = 4; white.height = 4;
        memset(white.rgb, 255, 48);
        os_image_ascii(&white, 4, ascii, sizeof ascii, &n);
        CHECK(ascii[0] == '@', "G18-IMG-ASCII-LUMINANCE-RAMP");
        memset(white.rgb, 0, 48);
        os_image_ascii(&white, 4, ascii, sizeof ascii, &n);
        CHECK(ascii[0] == ' ', "G18-IMG-ASCII-BLACK-IS-BLANK");
    }
}

static const char *PAGE_INDEX = "<html><head><title>Home</title></head><body><h1>Index</h1><p>Go to <a href=\"/two.html\">two</a> or <a href=\"sub/three.html\">three</a> or <a href=\"javascript:alert(1)\">bad</a> or <a href=\"http://evil.test/\">ext</a>.</p><img src=\"/pic.bmp\"></body></html>";

static void setup_site(void)
{
    uint8_t bmp[128];
    size_t n = make_bmp(bmp, 0);
    reset_net();
    add_html("site.test", "/", PAGE_INDEX);
    add_html("site.test", "/two.html", "<title>Two</title><p>second page <a href=\"/\">home</a></p>");
    add_html("site.test", "/sub/three.html", "<title>Three</title><p>third <a href=\"../two.html\">up</a></p>");
    add_html("evil.test", "/", "<p>evil</p>");
    add_route("site.test", "/pic.bmp", 200, "image/bmp", bmp, n);
    add_route("site.test", "/pic.png", 200, "image/png", "\x89PNG\r\n\x1a\n....", 12);
    add_route("site.test", "/notimg.bmp", 200, "text/html", bmp, n);
    add_route("site.test", "/data.json", 200, "application/json", "{}", 2);
    add_html("site.test", "/script.html", "<p>x</p><script>var i = 0; while (i < 3) { print(i); i = i + 1; }</script>");
    add_html("site.test", "/scripterr.html", "<p>kept</p><script>print(1/0);</script><script>print('second');</script>");
    add_html("site.test", "/net.html", "<p>n</p><script>fetch('http://evil.test/'); print('x');</script>");
    add_html("site.test", "/broken.html", "<!-- never closed");
    add_route("site.test", "/gone", 404, "text/html", "<p>gone</p>", 11);
    add_redirect("site.test", "/old", "/two.html");
    {
        char long_page[4000] = "<pre>";
        for (int i = 0; i < 40; i++) { char l[32]; snprintf(l, sizeof l, "line %d\n", i + 1); strcat(long_page, l); }
        strcat(long_page, "</pre>");
        add_html("site.test", "/long.html", long_page);
    }
    os_browser_init(&br, transport, 0);
    os_browser_allow_host(&br, "site.test");
}

static void test_browser(void)
{
    char view[1024];
    unsigned lines;
    setup_site();
    CHECK(os_browser_follow_link(&br, 1) == OS_BR_ERR_NOPAGE && os_browser_back(&br) == OS_BR_ERR_NOPAGE && os_browser_reload(&br) == OS_BR_ERR_NOPAGE &&
          os_browser_fetch_image(&br, 1, &img) == OS_BR_ERR_NOPAGE && os_browser_navigate(0, "http://site.test/") == OS_BR_ERR_ARGUMENT, "G18-BROWSER-OPERATIONS-REQUIRE-A-PAGE");
    CHECK(os_browser_navigate(&br, "http://site.test/") == 0 && br.has_page && !strcmp(br.title, "Home") && strstr(os_browser_text(&br), "INDEX") && strstr(os_browser_text(&br), "two[1]") &&
          br.history_count == 1 && br.last_status == 200, "G18-BROWSER-NAVIGATE-RENDERS-PAGE-AND-TITLE");
    CHECK(strstr(last_request, "GET / HTTP/1.1\r\nHost: site.test\r\n") && !strstr(last_request, "Cookie"), "G18-BROWSER-SENDS-MINIMAL-REQUEST");
    CHECK(os_browser_follow_link(&br, 1) == 0 && !strcmp(br.title, "Two") && br.history_count == 2 && br.history_pos == 1, "G18-BROWSER-FOLLOW-LINK");
    CHECK(os_browser_back(&br) == 0 && !strcmp(br.title, "Home") && os_browser_forward(&br) == 0 && !strcmp(br.title, "Two") && os_browser_forward(&br) == OS_BR_ERR_NOHISTORY, "G18-BROWSER-BACK-AND-FORWARD");
    os_browser_back(&br);
    CHECK(os_browser_follow_link(&br, 2) == 0 && !strcmp(br.title, "Three") && os_browser_follow_link(&br, 1) == 0 && !strcmp(br.title, "Two") && br.history_count == 3,
          "G18-BROWSER-RELATIVE-LINKS-RESOLVE-AGAINST-CURRENT-PAGE");
    os_browser_back(&br); os_browser_back(&br);
    CHECK(os_browser_follow_link(&br, 1) == 0 && br.history_count == 2 && os_browser_forward(&br) == OS_BR_ERR_NOHISTORY, "G18-BROWSER-NAVIGATION-DISCARDS-FORWARD-HISTORY");
    os_browser_navigate(&br, "http://site.test/");
    {
        unsigned c0 = calls;
        CHECK(os_browser_follow_link(&br, 3) != 0 && calls == c0 && !strcmp(br.title, "Home"), "G18-BROWSER-JAVASCRIPT-LINK-REFUSED-WITHOUT-NETWORK");
        CHECK(os_browser_follow_link(&br, 4) == OS_NET_ERR_DENIED && calls == c0 && !strcmp(br.title, "Home"), "G18-BROWSER-OFF-LIST-LINK-DENIED-WITHOUT-NETWORK");
        CHECK(os_browser_follow_link(&br, 9) == OS_BR_ERR_LINK && os_browser_follow_link(&br, 0) == OS_BR_ERR_LINK, "G18-BROWSER-INVALID-LINK-NUMBER-REFUSED");
    }
    {
        unsigned hc = br.history_count, hp = br.history_pos;
        char before[OS_BROWSER_TEXT_MAX];
        snprintf(before, sizeof before, "%s", os_browser_text(&br));
        CHECK(os_browser_navigate(&br, "http://site.test/gone") == OS_BR_ERR_STATUS && br.last_status == 404, "G18-BROWSER-HTTP-ERROR-STATUS-REPORTED");
        CHECK(os_browser_navigate(&br, "http://site.test/data.json") == OS_BR_ERR_TYPE, "G18-BROWSER-NON-HTML-DOCUMENT-REFUSED");
        CHECK(os_browser_navigate(&br, "http://site.test/broken.html") == OS_HTML_ERR_SYNTAX, "G18-BROWSER-UNPARSEABLE-PAGE-REFUSED");
        CHECK(os_browser_navigate(&br, "http://evil.test/") == OS_NET_ERR_DENIED && os_browser_navigate(&br, "https://site.test/") == OS_NET_ERR_TLS &&
              os_browser_navigate(&br, "ftp://site.test/") == OS_NET_ERR_SCHEME && os_browser_navigate(&br, "http://site.test/nothing") == OS_BR_ERR_STATUS, "G18-BROWSER-POLICY-AND-SCHEME-FAILURES-REPORTED");
        transport_fail = 1;
        CHECK(os_browser_navigate(&br, "http://site.test/two.html") == OS_NET_ERR_TRANSPORT, "G18-BROWSER-TRANSPORT-FAILURE-REPORTED");
        transport_fail = 0;
        CHECK(br.history_count == hc && br.history_pos == hp && !strcmp(os_browser_text(&br), before) && !strcmp(br.title, "Home"), "G18-BROWSER-FAILED-NAVIGATION-IS-ATOMIC");
    }
    CHECK(os_browser_navigate(&br, "http://site.test/old") == 0 && !strcmp(br.title, "Two") && !strcmp(br.history[br.history_pos].path, "/two.html"), "G18-BROWSER-REDIRECT-FOLLOWED-AND-RECORDED");
    CHECK(os_browser_navigate(&br, "http://site.test/script.html") == 0 && strstr(os_browser_text(&br), "[script output]\n0\n1\n2") && br.scripts_run == 1 && br.scripts_failed == 0, "G18-BROWSER-SCRIPT-OUTPUT-SHOWN");
    CHECK(os_browser_navigate(&br, "http://site.test/scripterr.html") == 0 && strstr(os_browser_text(&br), "kept") && strstr(os_browser_text(&br), "[script error: division trap]") &&
          strstr(os_browser_text(&br), "second") && br.scripts_run == 2 && br.scripts_failed == 1, "G18-BROWSER-SCRIPT-ERROR-IS-ISOLATED-AND-PAGE-STILL-RENDERS");
    {
        unsigned c0;
        os_browser_navigate(&br, "http://site.test/net.html");
        c0 = calls;
        CHECK(br.scripts_failed == 1 && strstr(os_browser_text(&br), "[script error:") && !strstr(os_browser_text(&br), "\nx\n") && calls == c0, "G18-BROWSER-SCRIPTS-HAVE-NO-NETWORK-ACCESS");
    }
    os_browser_navigate(&br, "http://site.test/long.html");
    lines = os_browser_line_count(&br);
    CHECK(lines == 40 && os_browser_view(&br, 5, view, sizeof view) == 5 && !strncmp(view, "line 1\nline 2\n", 14), "G18-BROWSER-VIEWPORT-SHOWS-REQUESTED-ROWS");
    os_browser_scroll(&br, 10);
    CHECK(os_browser_view(&br, 3, view, sizeof view) == 3 && !strncmp(view, "line 11\nline 12\nline 13\n", 24) && br.scroll == 10, "G18-BROWSER-SCROLLING");
    os_browser_scroll(&br, 1000);
    CHECK(br.scroll == 39 && os_browser_view(&br, 10, view, sizeof view) == 1, "G18-BROWSER-SCROLL-CLAMPED-AT-END");
    os_browser_scroll(&br, -1000);
    CHECK(br.scroll == 0, "G18-BROWSER-SCROLL-CLAMPED-AT-START");
    CHECK(os_browser_reload(&br) == 0 && br.scroll == 0, "G18-BROWSER-RELOAD");
    os_browser_navigate(&br, "http://site.test/");
    CHECK(os_browser_fetch_image(&br, 1, &img) == 0 && img.width == 2 && img.height == 2, "G18-BROWSER-IMAGE-FETCHED-AND-DECODED-UNDER-POLICY");
    CHECK(os_browser_fetch_image(&br, 2, &img) == OS_BR_ERR_LINK, "G18-BROWSER-UNKNOWN-IMAGE-REFUSED");
    add_html("site.test", "/imgs.html", "<img src=\"/pic.png\"><img src=\"/notimg.bmp\"><img src=\"http://evil.test/p.bmp\"><img src=\"javascript:x\">");
    os_browser_navigate(&br, "http://site.test/imgs.html");
    CHECK(os_browser_fetch_image(&br, 1, &img) == OS_BR_ERR_TYPE, "G18-BROWSER-IMAGE-WITH-UNSUPPORTED-TYPE-REFUSED");
    CHECK(os_browser_fetch_image(&br, 2, &img) == OS_BR_ERR_TYPE, "G18-BROWSER-IMAGE-CONTENT-TYPE-MUST-MATCH");
    CHECK(os_browser_fetch_image(&br, 3, &img) == OS_NET_ERR_DENIED && os_browser_fetch_image(&br, 4, &img) == OS_NET_ERR_SCHEME, "G18-BROWSER-IMAGE-FETCH-OBEYS-ALLOW-LIST-AND-SCHEMES");
    {
        os_browser_navigate(&br, "http://site.test/");
        for (int i = 0; i < 12; i++) os_browser_navigate(&br, i % 2 ? "http://site.test/" : "http://site.test/two.html");
        CHECK(br.history_count == OS_BROWSER_HISTORY && br.history_pos == OS_BROWSER_HISTORY - 1u, "G18-BROWSER-HISTORY-BOUNDED");
    }
}

static void test_documents(void)
{
    static os_document d;
    static os_doc_layout lay;
    static char out[OS_PRINT_MAX_PAGE_BYTES];
    static os_storage_fs fs;
    static uint8_t disk[512][OS_STORAGE_BLOCK_SIZE];
    (void)disk; (void)fs;
    CHECK(os_doc_set_text(&d, "a.txt", "hi\nthere", 8) == 0 && d.length == 8 && os_doc_set_text(&d, "a", "bad\x01", 4) == OS_DOC_ERR_CHARACTER && os_doc_set_text(&d, "", "x", 1) == OS_DOC_ERR_ARGUMENT &&
          os_doc_set_text(&d, "a", "tab\there", 8) == OS_DOC_ERR_CHARACTER && os_doc_set_text(&d, "a", 0, 1) == OS_DOC_ERR_ARGUMENT && d.length == 8, "G18-DOC-TEXT-VALIDATED-AND-FAILURE-KEEPS-DOCUMENT");
    {
        static char big[OS_DOC_MAX_BYTES + 1];
        memset(big, 'a', sizeof big);
        CHECK(os_doc_set_text(&d, "big", big, OS_DOC_MAX_BYTES) == 0 && os_doc_set_text(&d, "big", big, OS_DOC_MAX_BYTES + 1) == OS_DOC_ERR_SIZE &&
              os_doc_set_text(&d, "name-that-is-way-too-long-for-doc", "x", 1) == OS_DOC_ERR_ARGUMENT, "G18-DOC-SIZE-AND-NAME-BOUNDED");
    }
    os_doc_set_text(&d, "p", "aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii jjjj kkkk llll mmmm nnnn oooo\n\nsecond paragraph", 92);
    CHECK(os_doc_layout_build(&d, 20, 5, &lay) == 0 && lay.lines == 6 && lay.pages == 2, "G18-DOC-LAYOUT-WRAPS-AND-PAGINATES");
    {
        int ok = 1;
        for (unsigned i = 0; i < lay.lines; i++) if (lay.line_len[i] > 20) ok = 0;
        CHECK(ok && lay.line_len[4] == 0, "G18-DOC-LAYOUT-LINES-NEVER-EXCEED-WIDTH-AND-KEEP-BLANK-LINES");
    }
    CHECK(os_doc_page_text(&d, &lay, 1, out, sizeof out) > 0 && strstr(out, "aaaa bbbb cccc dddd\n") && strstr(out, "[page 1 of 2]\n") &&
          os_doc_page_text(&d, &lay, 2, out, sizeof out) > 0 && strstr(out, "second paragraph") && strstr(out, "[page 2 of 2]\n") &&
          os_doc_page_text(&d, &lay, 0, out, sizeof out) == OS_DOC_ERR_RANGE && os_doc_page_text(&d, &lay, 3, out, sizeof out) == OS_DOC_ERR_RANGE &&
          os_doc_page_text(&d, &lay, 1, out, 10) == OS_DOC_ERR_SIZE, "G18-DOC-PAGE-TEXT-FOOTER-RANGE-AND-BOUNDS");
    os_doc_set_text(&d, "e", "", 0);
    CHECK(os_doc_layout_build(&d, 20, 5, &lay) == 0 && lay.lines == 0 && lay.pages == 1 && os_doc_page_text(&d, &lay, 1, out, sizeof out) > 0, "G18-DOC-EMPTY-DOCUMENT-HAS-ONE-PAGE");
    os_doc_set_text(&d, "n", "line\n", 5);
    CHECK(os_doc_layout_build(&d, 20, 5, &lay) == 0 && lay.lines == 1, "G18-DOC-TRAILING-NEWLINE-ADDS-NO-BLANK-LINE");
    os_doc_set_text(&d, "w", "abcdefghijklmnopqrstuvwxyz", 26);
    CHECK(os_doc_layout_build(&d, 20, 5, &lay) == 0 && lay.lines == 2 && lay.line_len[0] == 20 && lay.line_len[1] == 6 && os_doc_layout_build(&d, 10, 5, &lay) == OS_DOC_ERR_ARGUMENT &&
          os_doc_layout_build(&d, 20, 99, &lay) == OS_DOC_ERR_ARGUMENT, "G18-DOC-OVERLONG-WORD-HARD-WRAPPED-AND-GEOMETRY-VALIDATED");
    {
        static char many[OS_DOC_MAX_BYTES];
        memset(many, '\n', sizeof many);
        os_doc_set_text(&d, "m", many, 600);
        CHECK(os_doc_layout_build(&d, 20, 5, &lay) == OS_DOC_ERR_LAYOUT, "G18-DOC-LINE-AND-PAGE-COUNT-BOUNDED");
    }
}

typedef struct { unsigned calls, fail_at; } sink_t;
static int counting_backend(void *ctx, unsigned job, unsigned copy, unsigned page, const char *t, size_t l)
{
    sink_t *s = (sink_t *)ctx;
    (void)job; (void)copy; (void)page; (void)t; (void)l;
    s->calls++;
    return (s->fail_at && s->calls == s->fail_at) ? -1 : 0;
}

static void test_printing(void)
{
    static os_print_spooler sp;
    static os_document d;
    static os_print_preview pv;
    os_print_backend pb;
    uint8_t dg[32], dg2[32];
    sink_t sink = { 0, 0 };
    os_print_backend counting = { "counting", counting_backend, &sink };
    int j, j2;
    os_doc_set_text(&d, "p", "aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii jjjj kkkk llll mmmm nnnn oooo\n\nsecond paragraph", 92);
    CHECK(os_print_init(&sp, &counting) == 0 && os_print_init(&sp, 0) == OS_DOC_ERR_ARGUMENT, "G18-PRINT-SPOOLER-INIT");
    j = os_print_submit(&sp, &d, 20, 5, 1, 2, 1);
    CHECK(j >= 0 && os_print_state(&sp, j) == OS_PJ_QUEUED, "G18-PRINT-SUBMIT-QUEUES-WITHOUT-PRINTING");
    CHECK(os_print_run(&sp, j) == OS_DOC_ERR_APPROVAL && sink.calls == 0 && os_print_state(&sp, j) == OS_PJ_QUEUED, "G18-PRINT-REFUSED-WITHOUT-OWNER-APPROVAL");
    os_print_job_digest(&sp, j, dg);
    memcpy(dg2, dg, 32); dg2[7] ^= 1;
    CHECK(os_print_approve(&sp, j, dg2) == OS_DOC_ERR_APPROVAL && os_print_state(&sp, j) == OS_PJ_QUEUED, "G18-PRINT-APPROVAL-BOUND-TO-JOB-DIGEST");
    sp.jobs[j].doc.text[0] ^= 1;
    CHECK(os_print_approve(&sp, j, dg) == 0 && os_print_run(&sp, j) == OS_DOC_ERR_APPROVAL && sink.calls == 0 && os_print_state(&sp, j) == OS_PJ_FAILED, "G18-PRINT-TAMPERED-QUEUED-JOB-REFUSED-AT-RUN-AND-NOT-RETRYABLE");
    j = os_print_submit(&sp, &d, 20, 5, 1, 2, 2);
    os_print_job_digest(&sp, j, dg);
    CHECK(os_print_approve(&sp, j, dg) == 0 && os_print_approve(&sp, j, dg) == OS_DOC_ERR_STATE, "G18-PRINT-APPROVAL-APPLIES-ONCE");
    CHECK(os_print_run(&sp, j) == 0 && sink.calls == 4 && os_print_state(&sp, j) == OS_PJ_DONE && sp.jobs[j].pages_done == 4, "G18-PRINT-RUNS-EVERY-PAGE-AND-COPY-THROUGH-BACKEND");
    CHECK(os_print_run(&sp, j) == OS_DOC_ERR_STATE && sink.calls == 4, "G18-PRINT-APPROVAL-IS-SINGLE-USE-REPRINT-REFUSED");
    j = os_print_submit(&sp, &d, 20, 5, 2, 2, 1);
    j2 = os_print_submit(&sp, &d, 20, 5, 1, 1, 1);
    CHECK(j >= 0 && j2 >= 0 && j != j2 && os_print_submit(&sp, &d, 20, 5, 1, 1, 1) == OS_DOC_ERR_FULL, "G18-PRINT-SPOOLER-QUEUE-BOUNDED");
    CHECK(os_print_cancel(&sp, j) == 0 && os_print_state(&sp, j) == OS_PJ_CANCELLED && os_print_cancel(&sp, j) == OS_DOC_ERR_STATE, "G18-PRINT-CANCEL");
    os_print_job_digest(&sp, j, dg);
    CHECK(os_print_approve(&sp, j, dg) == OS_DOC_ERR_STATE && os_print_run(&sp, j) == OS_DOC_ERR_STATE, "G18-PRINT-CANCELLED-JOB-CANNOT-BE-APPROVED-OR-RUN");
    CHECK(os_print_submit(&sp, &d, 20, 5, 1, 1, 1) == j, "G18-PRINT-FINISHED-SLOT-REUSED");
    os_print_cancel(&sp, j); os_print_cancel(&sp, j2);
    CHECK(os_print_submit(&sp, &d, 20, 5, 0, 1, 1) == OS_DOC_ERR_RANGE && os_print_submit(&sp, &d, 20, 5, 2, 1, 1) == OS_DOC_ERR_RANGE && os_print_submit(&sp, &d, 20, 5, 1, 3, 1) == OS_DOC_ERR_RANGE &&
          os_print_submit(&sp, &d, 20, 5, 1, 1, 0) == OS_DOC_ERR_RANGE && os_print_submit(&sp, &d, 20, 5, 1, 1, OS_PRINT_MAX_COPIES + 1u) == OS_DOC_ERR_RANGE &&
          os_print_submit(&sp, 0, 20, 5, 1, 1, 1) == OS_DOC_ERR_ARGUMENT && os_print_submit(&sp, &d, 20, 99, 1, 1, 1) == OS_DOC_ERR_ARGUMENT, "G18-PRINT-RANGE-COPY-AND-GEOMETRY-LIMITS");
    os_print_job_digest(&sp, 0, dg);
    j = os_print_submit(&sp, &d, 20, 5, 1, 2, 1);
    os_print_job_digest(&sp, j, dg2);
    {
        os_print_job_digest(&sp, j, dg);
        os_print_cancel(&sp, j);
        j = os_print_submit(&sp, &d, 20, 5, 1, 1, 1);
        os_print_job_digest(&sp, j, dg2);
        CHECK(memcmp(dg, dg2, 32) != 0, "G18-PRINT-DIGEST-COVERS-JOB-PARAMETERS");
        os_print_cancel(&sp, j);
        os_doc_set_text(&d, "p", "different", 9);
        j = os_print_submit(&sp, &d, 20, 5, 1, 1, 1);
        os_print_job_digest(&sp, j, dg);
        CHECK(memcmp(dg, dg2, 32) != 0, "G18-PRINT-DIGEST-COVERS-DOCUMENT-CONTENT");
        os_print_cancel(&sp, j);
    }
    os_doc_set_text(&d, "p", "aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii jjjj kkkk llll mmmm nnnn oooo\n\nsecond paragraph", 92);
    os_print_backend_preview(&pb, &pv);
    os_print_init(&sp, &pb);
    j = os_print_submit(&sp, &d, 20, 5, 1, 2, 1);
    os_print_job_digest(&sp, j, dg);
    os_print_approve(&sp, j, dg);
    CHECK(os_print_run(&sp, j) == 0 && pv.pages == 2 && strstr(pv.data, "[page 1 of 2]") && strstr(pv.data, "[page 2 of 2]") && strchr(pv.data, '\f'), "G18-PRINT-PREVIEW-BACKEND-RECEIVES-PAGES");
    os_print_init(&sp, &os_print_backend_hardware_unavailable);
    j = os_print_submit(&sp, &d, 20, 5, 1, 1, 1);
    os_print_job_digest(&sp, j, dg);
    os_print_approve(&sp, j, dg);
    CHECK(os_print_run(&sp, j) == OS_DOC_ERR_BACKEND && os_print_state(&sp, j) == OS_PJ_FAILED && sp.jobs[j].pages_done == 0, "G18-PRINT-HARDWARE-BACKEND-FAILS-CLOSED-NO-DRIVER");
    sink.calls = 0; sink.fail_at = 2;
    os_print_init(&sp, &counting);
    j = os_print_submit(&sp, &d, 20, 5, 1, 2, 2);
    os_print_job_digest(&sp, j, dg);
    os_print_approve(&sp, j, dg);
    CHECK(os_print_run(&sp, j) == OS_DOC_ERR_BACKEND && os_print_state(&sp, j) == OS_PJ_FAILED && sp.jobs[j].pages_done == 1 && sink.calls == 2, "G18-PRINT-BACKEND-FAILURE-MIDWAY-STOPS-JOB-AND-RECORDS-PROGRESS");
    CHECK(os_print_run(&sp, j) == OS_DOC_ERR_STATE && os_print_state(&sp, 99) == -1 && os_print_approve(&sp, -1, dg) == OS_DOC_ERR_ARGUMENT && os_print_run(&sp, 7) == OS_DOC_ERR_ARGUMENT,
          "G18-PRINT-FAILED-JOB-NOT-RETRYABLE-AND-IDS-VALIDATED");
}

static os_storage_fs sfs;
static uint8_t sdisk[256][OS_STORAGE_BLOCK_SIZE];
static int sread(void *c, u32 b, u8 d[OS_STORAGE_BLOCK_SIZE]) { (void)c; if (b >= 256) return 0; memcpy(d, sdisk[b], OS_STORAGE_BLOCK_SIZE); return 1; }
static int swrite(void *c, u32 b, const u8 d[OS_STORAGE_BLOCK_SIZE]) { (void)c; if (b >= 256) return 0; memcpy(sdisk[b], d, OS_STORAGE_BLOCK_SIZE); return 1; }
static os_storage_device sdev = { 0, 256, sread, swrite };

static void test_document_storage(void)
{
    static os_document a, b;
    const char *text = "Saved line one\nSaved line two\n";
    memset(sdisk, 0, sizeof sdisk);
    os_storage_format(&sdev);
    os_storage_mount(&sfs, &sdev);
    os_doc_set_text(&a, "report", text, strlen(text));
    CHECK(os_doc_save(&a, &sfs, "/doc/report") == 0 && os_doc_load(&b, &sfs, "/doc/report", "copy") == 0 && b.length == a.length && !memcmp(a.text, b.text, a.length) && !strcmp(b.name, "copy"), "G18-DOC-SAVE-AND-LOAD-ROUNDTRIP");
    CHECK(os_doc_load(&b, &sfs, "/doc/missing", "x") == OS_DOC_ERR_STORAGE && os_doc_save(&a, &sfs, "no-slash") == OS_DOC_ERR_STORAGE && os_doc_save(0, &sfs, "/x") == OS_DOC_ERR_ARGUMENT, "G18-DOC-MISSING-AND-INVALID-PATHS-REFUSED");
    os_storage_write(&sfs, "/doc/bin", "bad\x01\x02", 5);
    CHECK(os_doc_load(&b, &sfs, "/doc/bin", "x") == OS_DOC_ERR_CHARACTER && b.length == a.length, "G18-DOC-LOAD-REJECTS-BINARY-AND-KEEPS-DOCUMENT");
}

int main(void)
{
    test_urls();
    test_http();
    test_net_policy();
    test_html();
    test_js();
    test_images();
    test_browser();
    test_documents();
    test_printing();
    test_document_storage();
    printf(failures ? "G18 BROWSER RESULT: FAIL (%d)\n" : "G18 BROWSER RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
