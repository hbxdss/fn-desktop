(function() {
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
