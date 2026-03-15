#pragma once
#include <QObject>
#include <QRegularExpression>
#include <QString>
#include <functional>

// Processes raw email HTML for display: sanitisation, image neutralisation,
// dark-mode injection, and thread-card preparation.
//
// Theme colours (darkBg, surfaceBg, lightText, borderColor) are set as QML
// properties once and reused on every call to prepare()/prepareThread().
class HtmlProcessor : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString darkBg    MEMBER m_darkBg)
    Q_PROPERTY(QString surfaceBg MEMBER m_surfaceBg)
    Q_PROPERTY(QString lightText MEMBER m_lightText)
    Q_PROPERTY(QString borderColor MEMBER m_borderColor)

public:
    explicit HtmlProcessor(QObject *parent = nullptr);

    // Sanitise raw email HTML: strip scripts/external stylesheets, fix
    // document structure, rewrite tracking redirect links, decode MIME noise.
    Q_INVOKABLE QString sanitize(const QString &rawHtml) const;

    // Inject baseline head (color-scheme, white bg) and optional dark-mode
    // CSS+JS.  Used for single-message view (full viewport layout).
    Q_INVOKABLE QString prepare(const QString &html, bool darkMode) const;

    // Like prepare() but with thread-card margins and blockquote collapsing.
    Q_INVOKABLE QString prepareThread(const QString &html, bool darkMode) const;

    // Collapse all blockquotes behind a single native <details> toggle.
    Q_INVOKABLE QString collapseBlockquotes(const QString &html) const;

    // Replace external <img> src with a transparent 1×1 gif, preserving
    // file://, data:, and cid: sources so inline attachments still render.
    Q_INVOKABLE QString neutralizeExternalImages(const QString &html) const;

    // Replace 1×1 third-party tracking pixel src with a transparent gif.
    Q_INVOKABLE QString neutralizeTrackingPixels(const QString &html,
                                                  const QString &senderDomain) const;

private:
    QString decodeQuotedPrintable(const QString &input) const;
    QString sanitizeTrackingLinks(const QString &html) const;
    QString buildDarkModeInjection() const;

    static QString extractTrackingRedirectUrl(const QString &href);
    static bool    isFirstPartyUrl(const QString &url, const QString &senderDomain);

    // Iterate every match of re in input; call fn(match) to produce the
    // replacement for that span.  Segments between matches are kept verbatim.
    static QString applyToEachMatch(
        const QString &input,
        const QRegularExpression &re,
        const std::function<QString(const QRegularExpressionMatch &)> &fn);

    QString m_darkBg;
    QString m_surfaceBg;
    QString m_lightText;
    QString m_borderColor;
};
