#include "htmlprocessor.h"
#include "utils.h"
#include <QRegularExpression>
#include <QUrl>

using namespace Qt::Literals::StringLiterals;

HtmlProcessor::HtmlProcessor(QObject *parent) : QObject(parent) {}

// ── Static helpers ───────────────────────────────────────────────────────────

QString HtmlProcessor::applyToEachMatch(
    const QString &input,
    const QRegularExpression &re,
    const std::function<QString(const QRegularExpressionMatch &)> &fn)
{
    QString result;
    result.reserve(input.size() + input.size() / 4);
    qsizetype lastEnd = 0;
    auto it = re.globalMatch(input);
    while (it.hasNext()) {
        const auto m = it.next();
        result.append(input.mid(lastEnd, m.capturedStart() - lastEnd));
        result.append(fn(m));
        lastEnd = m.capturedEnd();
    }
    result.append(input.mid(lastEnd));
    return result;
}

QString HtmlProcessor::extractTrackingRedirectUrl(const QString &href)
{
    static const QRegularExpression re(
        R"([?&](?:redirect|url|to|link|target|dest|goto)=(https?(?:%3A%2F%2F|://)[^&]*))",
        QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(href);
    if (!m.hasMatch()) { return {}; }
    return QUrl::fromPercentEncoding(m.captured(1).toUtf8());
}

bool HtmlProcessor::isFirstPartyUrl(const QString &url, const QString &senderDomain)
{
    if (url.isEmpty() || senderDomain.isEmpty()) { return false; }
    static const QRegularExpression re(
        R"(^https?://([^/?#]+))",
        QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(url);
    if (!m.hasMatch()) { return false; }
    const QStringList urlParts = m.captured(1).split('.');
    if (urlParts.size() < 2) { return false; }
    const QString urlSld = urlParts[urlParts.size() - 2].toLower();
    const QStringList senderParts = senderDomain.toLower().split('.');
    if (senderParts.size() < 2) { return false; }
    return urlSld == senderParts[senderParts.size() - 2];
}

// ── Private helpers ──────────────────────────────────────────────────────────

QString HtmlProcessor::decodeQuotedPrintable(const QString &input) const
{
    if (input.isEmpty()) { return input; }
    QString s = input;
    static const QRegularExpression softBreakRe(R"(=\r?\n)");
    s.remove(softBreakRe);
    static const QRegularExpression hexRe(R"(=([0-9A-Fa-f]{2}))");
    s = applyToEachMatch(s, hexRe, [](const QRegularExpressionMatch &m) -> QString {
        bool ok;
        const int code = m.captured(1).toInt(&ok, 16);
        if (!ok) return m.captured(0);
        return QString(QChar(code));
    });
    return s;
}

QString HtmlProcessor::sanitizeTrackingLinks(const QString &html) const
{
    static const QRegularExpression re(
        R"((<a\b[^>]*\bhref\s*=\s*)(["'])(https?://[^"']{20,})\2)",
        QRegularExpression::CaseInsensitiveOption);
    return applyToEachMatch(html, re, [](const QRegularExpressionMatch &m) -> QString {
        const QString dest = extractTrackingRedirectUrl(m.captured(3));
        if (!dest.isEmpty()) {
            return m.captured(1) + m.captured(2) + dest + m.captured(2);
        }
        return m.captured(0);
    });
}

QString HtmlProcessor::buildDarkModeInjection() const
{
    // Common near-white bgcolor values used by marketing/transactional emails.
    // CSS !important in an author stylesheet beats both presentational hints (bgcolor attr)
    // and non-!important inline styles, so this is more reliable than JS for this pattern.
    static const QLatin1StringView kBgColorSelectors(
        "[bgcolor='#ffffff'],[bgcolor='#FFFFFF'],[bgcolor='#fff'],[bgcolor='#FFF'],[bgcolor='white'],"
        "[bgcolor='#f9f9f9'],[bgcolor='#F9F9F9'],[bgcolor='#fafafa'],[bgcolor='#FAFAFA'],"
        "[bgcolor='#f8f8f8'],[bgcolor='#F8F8F8'],[bgcolor='#f5f5f5'],[bgcolor='#F5F5F5'],"
        "[bgcolor='#f0f0f0'],[bgcolor='#F0F0F0'],[bgcolor='#eeeeee'],[bgcolor='#EEEEEE'],"
        "[bgcolor='#e8e8e8'],[bgcolor='#E8E8E8'],[bgcolor='#e0e0e0'],[bgcolor='#E0E0E0'],"
        "[bgcolor='#d9d9d9'],[bgcolor='#D9D9D9']");
    // Same coverage for inline style="background-color: ..." (CSS !important beats non-!important inline).
    static const QLatin1StringView kInlineStyleSelectors(
        "[style*='background-color:#fff'],[style*='background-color: #fff'],"
        "[style*='background-color:#FFF'],[style*='background-color: #FFF'],"
        "[style*='background-color:white'],[style*='background-color: white'],"
        "[style*='background-color:#ffffff'],[style*='background-color: #ffffff'],"
        "[style*='background-color:#FFFFFF'],[style*='background-color: #FFFFFF'],"
        "[style*='background-color:#f9f9f9'],[style*='background-color: #f9f9f9'],"
        "[style*='background-color:#F9F9F9'],[style*='background-color: #F9F9F9'],"
        "[style*='background-color:#fafafa'],[style*='background-color: #fafafa'],"
        "[style*='background-color:#FAFAFA'],[style*='background-color: #FAFAFA'],"
        "[style*='background-color:#f8f8f8'],[style*='background-color: #f8f8f8'],"
        "[style*='background-color:#F8F8F8'],[style*='background-color: #F8F8F8'],"
        "[style*='background-color:#f5f5f5'],[style*='background-color: #f5f5f5'],"
        "[style*='background-color:#F5F5F5'],[style*='background-color: #F5F5F5'],"
        "[style*='background-color:#f0f0f0'],[style*='background-color: #f0f0f0'],"
        "[style*='background-color:#F0F0F0'],[style*='background-color: #F0F0F0'],"
        "[style*='background-color:#eeeeee'],[style*='background-color: #eeeeee'],"
        "[style*='background-color:#EEEEEE'],[style*='background-color: #EEEEEE'],"
        "[style*='background-color:#e8e8e8'],[style*='background-color: #e8e8e8'],"
        "[style*='background-color:#E8E8E8'],[style*='background-color: #E8E8E8'],"
        "[style*='background-color:rgb(255'],[style*='background-color: rgb(255'],"
        "[style*='background-color:rgb(249'],[style*='background-color: rgb(249'],"
        "[style*='background-color:rgb(250'],[style*='background-color: rgb(250'],"
        "[style*='background-color:rgb(248'],[style*='background-color: rgb(248'],"
        "[style*='background-color:rgb(245'],[style*='background-color: rgb(245'],"
        "[style*='background-color:rgb(240'],[style*='background-color: rgb(240'],"
        "[style*='background:white'],[style*='background: white'],"
        "[style*='background:#fff'],[style*='background: #fff'],"
        "[style*='background:#FFF'],[style*='background: #FFF'],"
        "[style*='background:#ffffff'],[style*='background: #ffffff'],"
        "[style*='background:#FFFFFF'],[style*='background: #FFFFFF']");

    const QString style =
        "<style data-dark-mode='baseline'>"
        "html, body { background:" + m_darkBg + " !important; color:" + m_lightText + " !important; }"
        "* { color:" + m_lightText + " !important; }"
        "a { color: #5b9bd5 !important; }"
        "td[style*='border'], div[style*='border'], table[style*='border'] { border-color:" + m_borderColor + " !important; }"
        "hr { border-color:" + m_borderColor + " !important; }"
        "img, svg, canvas, video, picture, iframe { filter:none !important; mix-blend-mode:normal !important; opacity:1 !important; }"
        + QLatin1StringView(kBgColorSelectors) + "{ background-color:" + m_darkBg + " !important; }"
        + QLatin1StringView(kInlineStyleSelectors) + "{ background-color:" + m_darkBg + " !important; }"
        // Catch any element with inline background set to a light color (hex starting with #E-F or named white).
        // The background shorthand isn't caught by background-color selectors.
        "[style*='background: #F'],[style*='background: #f'],[style*='background: #E'],[style*='background: #e'],"
        "[style*='background:#F'],[style*='background:#f'],[style*='background:#E'],[style*='background:#e'],"
        "[style*='background: #D'],[style*='background: #d'],[style*='background:#D'],[style*='background:#d'],"
        "[style*='background: #C'],[style*='background: #c'],[style*='background:#C'],[style*='background:#c']"
        "{ background:" + m_darkBg + " !important; }"
        + "</style>"_L1;

    const QString colorVars =
        "var DARK_BG='"    + m_darkBg    + "',"
        "SURFACE_BG='"     + m_surfaceBg + "',"
        "LIGHT_TEXT='"     + m_lightText + "',"
        "BORDER_COLOR='"   + m_borderColor + "';"_L1;

    // Static JS body (color vars injected above; raw string preserves all regex backslashes).
    static const QString jsBody = QLatin1StringView(R"js(
var MEDIA={IMG:1,SVG:1,CANVAS:1,VIDEO:1,PICTURE:1,IFRAME:1};
function parseRGB(c){if(!c)return null;var m=c.match(/rgba?\(\s*(\d+),\s*(\d+),\s*(\d+)/);return m?[+m[1],+m[2],+m[3]]:null;}
function lum(r,g,b){var a=[r/255,g/255,b/255];for(var i=0;i<3;i++){a[i]=a[i]<=0.03928?a[i]/12.92:Math.pow((a[i]+0.055)/1.055,2.4);}return 0.2126*a[0]+0.7152*a[1]+0.0722*a[2];}
function isLight(c){var r=parseRGB(c);return r?lum(r[0],r[1],r[2])>0.35:false;}
function isTransparent(c){if(!c)return true;return c==='transparent'||/rgba?\(.*,\s*0\)$/.test(c);}
function isSaturated(rgb){return Math.max(rgb[0],rgb[1],rgb[2])-Math.min(rgb[0],rgb[1],rgb[2])>20;}
function isImageShell(el){if(!el.querySelector||!el.querySelector('img,svg,canvas,video,picture'))return false;return(el.textContent||'').replace(/\s+/g,'').length===0;}
function isTextLink(el){return !el.querySelector('div,table,p,h1,h2,h3,h4,h5,h6,section,article,header,footer');}
function effectiveBg(el,doc){var cur=el;while(cur){var b=doc.defaultView.getComputedStyle(cur).backgroundColor;if(!isTransparent(b))return b;cur=cur.parentElement;}return DARK_BG;}
function hexToRGB(h){if(!h)return null;h=h.replace('#','').trim();if(h.length===3)h=h[0]+h[0]+h[1]+h[1]+h[2]+h[2];var n=parseInt(h,16);return isNaN(n)?null:[(n>>16)&255,(n>>8)&255,n&255];}
function getBgRGB(el,cs){var rgb=parseRGB(cs.backgroundColor);if(rgb)return rgb;var raw=(el.getAttribute('style')||'');var hm=raw.match(/background\s*:\s*#([0-9a-fA-F]{3,8})/);if(hm)return hexToRGB(hm[1]);var bgAttr=el.getAttribute('bgcolor');if(bgAttr)return hexToRGB(bgAttr);return null;}
function processEl(el,doc,origMap){if(MEDIA[el.tagName])return;var cs=doc.defaultView.getComputedStyle(el);if(!cs)return;var fg=cs.color,hasBgImg=(cs.backgroundImage&&cs.backgroundImage!=='none')||el.hasAttribute('background');var rgb=getBgRGB(el,cs);if(rgb&&!hasBgImg&&lum(rgb[0],rgb[1],rgb[2])>0.7&&!isSaturated(rgb)){if(isImageShell(el)){el.style.setProperty('background','transparent','important');}else{var tgt=lum(rgb[0],rgb[1],rgb[2])>0.97?DARK_BG:SURFACE_BG;el.style.setProperty('background',tgt,'important');}}var fgR=parseRGB(fg);if(fgR){var fgL=lum(fgR[0],fgR[1],fgR[2]);if(fgL<0.35){var eff=effectiveBg(el,doc);var effR=parseRGB(eff)||[30,30,30];var effL=lum(effR[0],effR[1],effR[2]);var lo=Math.min(fgL,effL),hi=Math.max(fgL,effL);if((hi+0.05)/(lo+0.05)<3){var target=LIGHT_TEXT;if(el.tagName==='A'){target=isTextLink(el)?'#7ab4f5':LIGHT_TEXT;}else if(Math.max(fgR[0],fgR[1],fgR[2])-Math.min(fgR[0],fgR[1],fgR[2])>150){target='#7ab4f5';}el.style.setProperty('color',target,'important');}}else if(fgL>0.5){var ownBg2=cs.backgroundColor;if(isTransparent(ownBg2)||isLight(ownBg2)){var eff2=effectiveBg(el,doc);if(isLight(eff2)){var origColor=(origMap&&origMap.get(el))||el.style.color||'rgb(0,0,0)';el.style.setProperty('color',origColor,'important');}}}}var bc=cs.borderColor;if(bc&&isLight(bc))el.style.setProperty('border-color',BORDER_COLOR,'important');}
function run(){var d=document;if(!d||!d.body)return;var all=d.querySelectorAll('*');var origMap=null;var myStyle=d.querySelector('style[data-dark-mode]');if(myStyle){origMap=new WeakMap();myStyle.disabled=true;for(var j=0;j<all.length;j++){try{origMap.set(all[j],d.defaultView.getComputedStyle(all[j]).color);}catch(e){}}myStyle.disabled=false;}for(var i=0;i<all.length;i++){try{processEl(all[i],d,origMap);}catch(e){console.log('kestrel-dark-error',e);}}}
function debugDark(){var b=document.body;if(!b)return;var cs=window.getComputedStyle(b);console.log('KESTREL-DARK body.bg='+cs.backgroundColor+' body.style='+b.getAttribute('style')+' body.bgImg='+cs.backgroundImage);var tds=document.querySelectorAll('td[id],td[style*=background]');for(var i=0;i<Math.min(tds.length,3);i++){var tcs=window.getComputedStyle(tds[i]);console.log('KESTREL-DARK td['+i+'].bg='+tcs.backgroundColor+' style='+tds[i].getAttribute('style').substring(0,80));}}
if(document.readyState==='complete'||document.readyState==='interactive'){run();debugDark();}else document.addEventListener('DOMContentLoaded',function(){run();debugDark();},{once:true});window.addEventListener('load',function(){run();debugDark();},{once:true});setTimeout(function(){run();debugDark();},500);
)js");

    const QString script = "<script>(function(){"_L1 + colorVars + jsBody + "})();</script>"_L1;
    return style + script;
}

// ── Public API ───────────────────────────────────────────────────────────────

QString HtmlProcessor::sanitize(const QString &rawHtml) const
{
    if (rawHtml.isEmpty()) { return "<html><body></body></html>"_L1; }

    QString html = rawHtml;
    html.replace("\r\n"_L1, "\n"_L1);

    // Remove binary/control characters that break WebEngine parsing.
    static const QRegularExpression ctrlRe(R"([\x00-\x08\x0B\x0C\x0E-\x1F])");
    html.remove(ctrlRe);

    // Strip external scripts (blocks DOMContentLoaded, security hygiene).
    static const QRegularExpression scriptRe(
        R"(<script\b[^>]*\bsrc\s*=[^>]*>[\s\S]*?</script>)",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    html.remove(scriptRe);

    // Strip external stylesheet links (web fonts cause render stalls).
    static const QRegularExpression linkRe(
        R"(<link\b[^>]*\brel\s*=\s*["']?stylesheet["']?[^>]*>)",
        QRegularExpression::CaseInsensitiveOption);
    html.remove(linkRe);

    // Rewrite known tracking redirect links.
    html = sanitizeTrackingLinks(html);

    // Convert markdown links [label](url) to real anchors.
    static const QRegularExpression mdLinkRe(
        R"(\[([^\]\n]{1,240})\]\((https?://[^\s)]+)\))",
        QRegularExpression::CaseInsensitiveOption);
    html = applyToEachMatch(html, mdLinkRe, [](const QRegularExpressionMatch &m) -> QString {
        return "<a href=\""_L1 + m.captured(2) + "\">"_L1 + m.captured(1) + "</a>"_L1;
    });

    // If already a full HTML document, return as-is.
    const QString trimmed = html.trimmed();
    static const QRegularExpression htmlOpenRe(R"(<html\b)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression htmlCloseEndRe(R"(</html>\s*$)", QRegularExpression::CaseInsensitiveOption);
    if (htmlOpenRe.match(trimmed).hasMatch() && htmlCloseEndRe.match(trimmed).hasMatch()) {
        return trimmed;
    }

    // If we got MIME-ish payload, cut to content after the first header break.
    static const QRegularExpression mimeHeaderRe(
        R"(^(mime-version:|content-type:|content-transfer-encoding:|x-[a-z0-9-]+:))",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
    if (mimeHeaderRe.match(html).hasMatch()) {
        const qsizetype splitAt = html.indexOf("\n\n"_L1);
        if (splitAt >= 0 && splitAt < html.size() - 2) {
            html = html.mid(splitAt + 2);
        }
    }

    // Drop IMAP protocol lines that occasionally leak into payload.
    {
        static const QRegularExpression imapFetchRe(R"(^\*\s+\d+\s+fetch\s*\()", QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression imapTagRe(R"(^[a-z]\d+\s+(ok|no|bad)\b)", QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression imapBodyRe(R"(^body\[header\.fields)", QRegularExpression::CaseInsensitiveOption);
        const QStringList lines = html.split('\n');
        QStringList kept;
        kept.reserve(lines.size());
        for (const QString &line : lines) {
            const QString t = line.trimmed();
            if (t.isEmpty()) { kept.append(line); continue; }
            if (imapFetchRe.match(t).hasMatch()) continue;
            if (imapTagRe.match(t).hasMatch()) continue;
            if (imapBodyRe.match(t).hasMatch()) continue;
            kept.append(line);
        }
        html = kept.join('\n');
    }

    // Decode quoted-printable artifacts when present.
    static const QRegularExpression qpCheckRe(R"(=\r?\n|=[0-9A-Fa-f]{2})");
    if (qpCheckRe.match(html).hasMatch()) {
        html = decodeQuotedPrintable(html);
    }

    // Strip MIME preamble lines that sometimes leak into body content.
    {
        static const QRegularExpression headerLikeRe(R"(^content-|^mime-version:|^x-)", QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression boundaryLikeRe(R"(^--[_=A-Za-z0-9.:-]+$)");
        const QStringList lines = html.split('\n');
        QStringList kept;
        kept.reserve(lines.size());
        bool stripping = true;
        for (int i = 0; i < lines.size(); ++i) {
            const QString t = lines[i].trimmed();
            if (stripping) {
                if (headerLikeRe.match(t).hasMatch() || boundaryLikeRe.match(t).hasMatch() || t.isEmpty()) {
                    if (t.isEmpty() && i > 0) {
                        stripping = false;
                    }
                    continue;
                }
            }
            stripping = false;
            kept.append(lines[i]);
        }
        html = kept.join('\n').trimmed();
    }

    // Determine document structure and build a full HTML document.
    static const QRegularExpression htmlTagRe(R"(<html\b)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression bodyTagRe(R"(<body\b)", QRegularExpression::CaseInsensitiveOption);
    if (!Kestrel::htmlishRe().match(html).hasMatch()) {
        QString escaped = html;
        escaped.replace('&', "&amp;"_L1);
        escaped.replace('<', "&lt;"_L1);
        escaped.replace('>', "&gt;"_L1);
        return "<!doctype html><html><head><meta charset='utf-8'></head>"
               "<body><pre style='white-space:pre-wrap;font-family:sans-serif;'>"_L1
               + escaped + "</pre></body></html>"_L1;
    }

    const bool hasHtmlTag = htmlTagRe.match(html).hasMatch();
    const bool hasBodyTag = bodyTagRe.match(html).hasMatch();

    if (hasHtmlTag) {
        static const QRegularExpression headTagRe(R"(<head\b)", QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression charsetRe(R"(<meta\s+charset=)", QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression htmlOpenTagRe(R"(<html[^>]*>)", QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression headOpenTagRe(R"(<head[^>]*>)", QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression htmlEndTagRe(R"(</html>\s*$)", QRegularExpression::CaseInsensitiveOption);

        if (!headTagRe.match(html).hasMatch()) {
            html = applyToEachMatch(html, htmlOpenTagRe, [](const QRegularExpressionMatch &m) -> QString {
                return m.captured(0) + "<head><meta charset='utf-8'></head>"_L1;
            });
        } else if (!charsetRe.match(html).hasMatch()) {
            html = applyToEachMatch(html, headOpenTagRe, [](const QRegularExpressionMatch &m) -> QString {
                return m.captured(0) + "<meta charset='utf-8'>"_L1;
            });
        }
        if (!hasBodyTag) {
            html = applyToEachMatch(html, htmlEndTagRe, [](const QRegularExpressionMatch &m) -> QString {
                Q_UNUSED(m)
                return "<body></body></html>"_L1;
            });
        }
        return html;
    }

    if (hasBodyTag) {
        return "<!doctype html><html><head><meta charset='utf-8'></head>"_L1 + html + "</html>"_L1;
    }

    return "<!doctype html><html><head><meta charset='utf-8'></head><body>"_L1 + html + "</body></html>"_L1;
}

QString HtmlProcessor::collapseBlockquotes(const QString &html) const
{
    static const QRegularExpression blockquoteRe(
        R"(<blockquote\b[\s\S]*?</blockquote>)",
        QRegularExpression::CaseInsensitiveOption);

    QStringList quoteBlocks;
    bool insertedMarker = false;

    QString result = applyToEachMatch(html, blockquoteRe, [&](const QRegularExpressionMatch &m) -> QString {
        quoteBlocks.append(m.captured(0));
        if (!insertedMarker) {
            insertedMarker = true;
            return "__KQ_SINGLE_QUOTE_BLOCK__"_L1;
        }
        return {};
    });

    if (!quoteBlocks.isEmpty()) {
        const QString merged =
            "<details class=\"kq\"><summary></summary><div class=\"kq-wrap\">"_L1
            + quoteBlocks.join(QString())
            + "</div></details>"_L1;
        result.replace("__KQ_SINGLE_QUOTE_BLOCK__"_L1, merged);
    }

    return result;
}

QString HtmlProcessor::neutralizeExternalImages(const QString &html) const
{
    static const QString blank =
        "data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7"_L1;
    static const QRegularExpression imgRe(R"(<img\b[^>]*>)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression srcRe(R"(\bsrc\s*=\s*(["'])([^"']+)\1)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression localSrcRe(R"(^(file:|data:|cid:))", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression httpSrcRe(R"(^https?:)", QRegularExpression::CaseInsensitiveOption);

    // Step 1: neutralize <img src="http..."> tags
    QString result = applyToEachMatch(html, imgRe, [&](const QRegularExpressionMatch &m) -> QString {
        QString tag = m.captured(0);
        const auto srcM = srcRe.match(tag);
        if (!srcM.hasMatch()) { return tag; }
        const QString src = srcM.captured(2);
        if (localSrcRe.match(src).hasMatch()) { return tag; }
        if (!httpSrcRe.match(src).hasMatch()) { return tag; }
        const QChar q = srcM.captured(1).at(0);
        return tag.left(srcM.capturedStart()) + "src="_L1 + q + blank + q + tag.mid(srcM.capturedEnd());
    });

    // Step 2: neutralize CSS background-image: url(http...) and background: ... url(http...)
    static const QRegularExpression bgUrlRe(
        R"(url\s*\(\s*(['"]?)(https?://[^)'"]*)\1\s*\))",
        QRegularExpression::CaseInsensitiveOption);
    result.replace(bgUrlRe, "url("_L1 + blank + ")"_L1);

    return result;
}

QString HtmlProcessor::neutralizeTrackingPixels(const QString &html,
                                                 const QString &senderDomain) const
{
    static const QString blank =
        "data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7"_L1;
    static const QRegularExpression imgRe(R"(<img\b[^>]*>)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression srcHttpRe(R"(\bsrc\s*=\s*["']https?:)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression width1Re(R"(\bwidth\s*=\s*["']\s*1\s*["'])", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression height1Re(R"(\bheight\s*=\s*["']\s*1\s*["'])", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression srcExtractRe(R"(\bsrc\s*=\s*(["'])([^"']+)\1)", QRegularExpression::CaseInsensitiveOption);

    return applyToEachMatch(html, imgRe, [&](const QRegularExpressionMatch &m) -> QString {
        QString tag = m.captured(0);
        if (!srcHttpRe.match(tag).hasMatch())  return tag;
        if (!width1Re.match(tag).hasMatch())   return tag;
        if (!height1Re.match(tag).hasMatch())  return tag;
        const auto srcM = srcExtractRe.match(tag);
        if (!srcM.hasMatch())                  return tag;
        if (isFirstPartyUrl(srcM.captured(2), senderDomain)) return tag;
        const QChar q = srcM.captured(1).at(0);
        return tag.left(srcM.capturedStart()) + "src="_L1 + q + blank + q + tag.mid(srcM.capturedEnd());
    });
}

static const QLatin1StringView kScrollbarCss(
    "<style data-kestrel-scrollbar='1'>"
    "html,body{scrollbar-width:thin;scrollbar-color:rgba(255,255,255,0.35) transparent;}"
    "::-webkit-scrollbar{width:5px;height:5px;}"
    "::-webkit-scrollbar-track{background:transparent;}"
    "::-webkit-scrollbar-thumb{background:rgba(255,255,255,0.35);border-radius:999px;}"
    "::-webkit-scrollbar-thumb:hover{background:rgba(255,255,255,0.5);}"
    "::-webkit-scrollbar-corner{background:transparent;}"
    "</style>");

QString
HtmlProcessor::injectHeadAndDarkMode(const QString &html,
                                      const QString &headInsert,
                                      const bool prependIfNoHead,
                                      const bool darkMode) const {
    auto result = html;

    // Inject head content (must come before dark mode injection so dark !important wins).
    const auto hp = result.indexOf("<head>"_L1, 0, Qt::CaseInsensitive);
    if (hp >= 0) {
        result.insert(hp + 6, headInsert);
    } else if (prependIfNoHead) {
        result = headInsert + result;
    }

    if (!darkMode) {
        return result;
    }

    // Inject dark mode style+script.  Insert after <head> so it precedes baseline.
    const auto injection = buildDarkModeInjection();
    const auto hp2 = result.indexOf("<head>"_L1, 0, Qt::CaseInsensitive);
    if (hp2 >= 0) {
        result.insert(hp2 + 6, injection);
    } else {
        static const QRegularExpression htmlOpenTagRe(R"(<html[^>]*>)", QRegularExpression::CaseInsensitiveOption);
        const auto hm = htmlOpenTagRe.match(result);
        if (hm.hasMatch()) {
            result.insert(hm.capturedEnd(), "<head>"_L1 + injection + "</head>"_L1);
        } else {
            result = "<html><head>"_L1 + injection + "</head><body>"_L1 + result + "</body></html>"_L1;
        }
    }

    return result;
}

QString
HtmlProcessor::prepare(const QString &html, const bool darkMode) const {
    static const QLatin1StringView baselineHead(
        "<meta name='color-scheme' content='light only'>"
        "<style data-kestrel-bg='baseline'>"
        "html,body{background-color:white;min-height:calc(100vh + 1px);}"
        "</style>");

    const auto headInsert = QLatin1StringView(baselineHead) + QLatin1StringView(kScrollbarCss);
    return injectHeadAndDarkMode(html, headInsert, true, darkMode);
}

QString
HtmlProcessor::prepareThread(const QString &html, const bool darkMode) const {
    static const QLatin1StringView quoteCss(
        "<style>"
        "details.kq{margin:4px 0}"
        "details.kq>summary{cursor:pointer;list-style:none;display:inline-block;"
        "background:rgba(128,128,128,0.15);border-radius:3px;padding:1px 8px;"
        "font-size:11px;opacity:.7;outline:none}"
        "details.kq>summary::-webkit-details-marker{display:none}"
        "details.kq>summary::before{content:'\\B7\\B7\\B7'}"
        "</style>");

    const auto baselineHead = darkMode
        ? "<meta name='color-scheme' content='dark'>"
          "<style data-kestrel-bg='baseline'>"
          "html,body{margin:8px 12px 0 12px;}"
          "</style>"_L1
        : "<meta name='color-scheme' content='light only'>"
          "<style data-kestrel-bg='baseline'>"
          "html,body{background-color:white;margin:8px 12px 0 12px;}"
          "</style>"_L1;

    const auto headInsert = baselineHead + QLatin1StringView(kScrollbarCss) + QLatin1StringView(quoteCss);
    return injectHeadAndDarkMode(html, headInsert, true, darkMode);
}
