(function() {
    // JS 层硬校验：仅当页面属于目标 NAS 域时才自动填充，防止密码外泄到其他网站
    var allowed = window.__fnosAllowedHosts || [];
    var cur = (location.hostname || '').toLowerCase();
    var ok = false;
    for (var i = 0; i < allowed.length; i++) {
        var a = (allowed[i] || '').toLowerCase();
        if (cur === a || cur.endsWith('.' + a) || a.endsWith('.' + cur)) { ok = true; break; }
    }
    if (!ok) return;

    if (window.__fnosFocusStarted) return;
    window.__fnosFocusStarted = true;

    function isVisible(el) {
        return !!(el.offsetWidth || el.offsetHeight || el.getClientRects().length);
    }

    function findUserField() {
        var inputs = document.querySelectorAll('input');
        var pwd = null;
        for (var i = 0; i < inputs.length; i++) {
            if ((inputs[i].type || '').toLowerCase() === 'password' && isVisible(inputs[i])) {
                pwd = inputs[i];
                break;
            }
        }
        if (!pwd) return null;
        for (var i = 0; i < inputs.length; i++) {
            var inp = inputs[i];
            if (inp === pwd) break;
            if (!isVisible(inp)) continue;
            var t = (inp.type || '').toLowerCase();
            if (t === 'text' || t === 'email' || t === 'tel' || t === '') {
                return inp;
            }
        }
        return null;
    }

    var tries = 0;
    var timer = setInterval(function() {
        tries++;
        var f = findUserField();
        if (f) {
            if (f.value) { clearInterval(timer); return; }
            f.focus();
            console.log('__FNOS_FOCUS_READY__');
            clearInterval(timer);
            return;
        }
        if (tries >= 80) clearInterval(timer);
    }, 250);
})();
