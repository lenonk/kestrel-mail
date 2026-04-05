.pragma library

function _replySubject(subj) {
    var s = (subj || "").toString().trim()
    return s.toLowerCase().indexOf("re:") === 0 ? s : "Re: " + s
}

function _fwdSubject(subj) {
    var s = (subj || "").toString().trim()
    var l = s.toLowerCase()
    return (l.indexOf("fwd:") === 0 || l.indexOf("fw:") === 0) ? s : "Fwd: " + s
}

function _forwardDateText(d, localeFn) {
    if (!d || !d.receivedAt)
        return ""
    return localeFn(new Date(d.receivedAt), "ddd M/d/yyyy h:mm AP")
}

function _quotedBody(d, localeFn) {
    var sender = (d.sender || "").replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;")
    var subj   = (d.subject || "").replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;")
    var date   = d.receivedAt ? localeFn(new Date(d.receivedAt), "ddd M/d/yyyy h:mm AP") : ""
    var body   = (d.bodyHtml && d.bodyHtml.toString().length > 0) ? d.bodyHtml : (d.snippet || "")
    return '<br><br><blockquote style="margin:0 0 0 6px;padding-left:8px;border-left:2px solid #999;color:inherit;">'
         + '<b>From:</b> ' + sender + '<br>'
         + (date ? '<b>Date:</b> ' + date + '<br>' : '')
         + '<b>Subject:</b> ' + subj + '<br><br>'
         + body
         + '</blockquote>'
}

// Builds {body, bodyText} for a quoted message compose window.
// body    → original HTML for WebEngineView (empty if plain-text source)
// bodyText → RichText header + optional plain text for the editable TextArea
// localeFn: function(date, format) → formatted string
function _buildQuotedContent(d, headerLabel, localeFn) {
    function esc(s) {
        return (s || "").toString()
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
            .replace(/>/g, "&gt;")
    }
    function mailtoLink(email) {
        return '<a href="mailto:' + email + '">' + esc(email) + '</a>'
    }
    function _fmtFromHtml(raw) {
        var s = (raw || "").toString().trim()
        var lt = s.lastIndexOf('<')
        var gt = s.lastIndexOf('>')
        if (lt > 0 && gt > lt) {
            var name = s.slice(0, lt).trim()
            var email = s.slice(lt + 1, gt).trim()
            return name.length
                ? '&quot;' + esc(name) + '&quot; &lt;' + mailtoLink(email) + '&gt;'
                : mailtoLink(email)
        }
        return s.indexOf('@') >= 0 ? mailtoLink(s) : esc(s)
    }
    function _fmtToHtml(raw) {
        var s = (raw || "").toString().trim()
        var lt = s.lastIndexOf('<')
        var gt = s.lastIndexOf('>')
        var email = (lt >= 0 && gt > lt) ? s.slice(lt + 1, gt).trim() : s
        return email.indexOf('@') >= 0 ? mailtoLink(email) : esc(email)
    }

    var originalHtml = (d.bodyHtml && d.bodyHtml.toString().length > 0) ? d.bodyHtml.toString() : ""
    var originalText = (d.body && d.body.toString().length > 0) ? d.body.toString() : ((d.snippet || "").toString())
    var senderRaw = (d.sender && d.sender.toString().length > 0) ? d.sender.toString() : (d.accountEmail || "").toString()
    var toRaw = (d.recipient || "").toString()
    var msgDate = d.receivedAt
        ? localeFn(new Date(d.receivedAt), "M/d/yyyy h:mm:ss AP")
        : ""

    var header = "<br>------ " + headerLabel + " ------<br>"
                 + "From: " + _fmtFromHtml(senderRaw) + "<br>"
                 + "To: " + _fmtToHtml(toRaw) + "<br>"
                 + (msgDate ? "Date: " + esc(msgDate) + "<br>" : "")
                 + "Subject: " + esc((d.subject || "").toString()) + "<br><br>"

    if (originalHtml.length > 0)
        return { body: originalHtml, bodyText: header }

    var escapedText = esc(originalText).replace(/\n/g, "<br>")
    return { body: "", bodyText: header + escapedText }
}
