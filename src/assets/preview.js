(function () {
    'use strict';

    const languageLabels = {
        bash: 'Shell', c: 'C', cpp: 'C++', csharp: 'C#', css: 'CSS',
        dos: 'Batch', html: 'HTML', ini: 'INI', java: 'Java', javascript: 'JavaScript',
        json: 'JSON', lisp: 'Lisp', lua: 'Lua', makefile: 'Makefile', markdown: 'Markdown',
        plaintext: 'Text', powershell: 'PowerShell', properties: 'Properties', python: 'Python',
        rust: 'Rust', scheme: 'Scheme', shell: 'Shell', sql: 'SQL', typescript: 'TypeScript',
        xml: 'XML', yaml: 'YAML'
    };

    const languageAliases = {
        bat: 'dos', batch: 'dos', cmd: 'dos', cs: 'csharp', htm: 'html', js: 'javascript',
        md: 'markdown', ps1: 'powershell', py: 'python', sh: 'bash', text: 'plaintext',
        scheme: 'lisp', ts: 'typescript', yml: 'yaml'
    };

    let findState = null;

    function clearFindHighlights() {
        if (window.CSS && CSS.highlights) {
            CSS.highlights.delete('editmdview-find');
            CSS.highlights.delete('editmdview-find-current');
        }
        findState = null;
    }

    function buildFindRanges(query) {
        const ranges = [];
        const needle = query.toLocaleLowerCase();
        if (!needle || !document.body) return ranges;
        const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT, {
            acceptNode(node) {
                const parent = node.parentElement;
                if (!parent || parent.closest('script,style,noscript,.editmdview-code-tools,.editmdview-outline')) {
                    return NodeFilter.FILTER_REJECT;
                }
                return node.data ? NodeFilter.FILTER_ACCEPT : NodeFilter.FILTER_REJECT;
            }
        });
        let node;
        while ((node = walker.nextNode()) && ranges.length < 5000) {
            const haystack = node.data.toLocaleLowerCase();
            let offset = 0;
            while (offset <= haystack.length - needle.length && ranges.length < 5000) {
                const found = haystack.indexOf(needle, offset);
                if (found < 0) break;
                const range = new Range();
                range.setStart(node, found);
                range.setEnd(node, found + needle.length);
                ranges.push(range);
                offset = found + Math.max(1, needle.length);
            }
        }
        return ranges;
    }

    function revealRange(range) {
        const bounds = range.getBoundingClientRect();
        const top = window.scrollY + bounds.top - Math.max(56, window.innerHeight * .24);
        window.scrollTo({ top: Math.max(0, top), behavior: 'smooth' });
    }

    function findInPreview(query, backwards, fromStart) {
        if (!query) {
            clearFindHighlights();
            return { index: 0, total: 0 };
        }
        if (!window.CSS || !CSS.highlights || typeof Highlight === 'undefined') {
            const found = window.find(query, false, Boolean(backwards), true, false, true, false);
            const result = { index: found ? 1 : 0, total: found ? 1 : 0 };
            try { window.chrome.webview.postMessage('editmdview-find-result:' + result.index + '/' + result.total); } catch (error) { /* Ignore. */ }
            return result;
        }

        const changed = !findState || findState.body !== document.body || findState.query !== query;
        if (changed) {
            const ranges = buildFindRanges(query);
            findState = { body: document.body, query, ranges, index: -1 };
            CSS.highlights.set('editmdview-find', new Highlight(...ranges));
        }
        const total = findState.ranges.length;
        if (total) {
            if (changed || fromStart || findState.index < 0) findState.index = backwards ? total - 1 : 0;
            else findState.index = (findState.index + (backwards ? -1 : 1) + total) % total;
            const current = findState.ranges[findState.index];
            CSS.highlights.set('editmdview-find-current', new Highlight(current));
            revealRange(current);
        } else {
            CSS.highlights.delete('editmdview-find-current');
            findState.index = -1;
        }
        const result = { index: total ? findState.index + 1 : 0, total };
        try { window.chrome.webview.postMessage('editmdview-find-result:' + result.index + '/' + result.total); } catch (error) { /* Ignore. */ }
        return result;
    }

    function explicitLanguage(code) {
        for (const name of code.classList) {
            const match = /^(?:lang|language)-(.+)$/i.exec(name);
            if (match) return match[1].toLowerCase();
        }
        return '';
    }

    function displayLanguage(language) {
        if (!language) return '';
        const canonical = languageAliases[language] || language;
        return languageLabels[canonical] || language.replace(/[-_]/g, ' ');
    }

    function installCodeBlocks() {
        document.querySelectorAll('pre').forEach(pre => {
            const code = pre.querySelector(':scope > code');
            const language = code ? explicitLanguage(code) : '';
            const canonical = languageAliases[language] || language;
            if (code && canonical && window.hljs && window.hljs.getLanguage(canonical) &&
                !code.dataset.highlighted) {
                if (canonical !== language && language) code.classList.add('language-' + canonical);
                window.hljs.highlightElement(code);
            }

            let wrapper = pre.parentElement;
            if (!wrapper || !wrapper.classList.contains('editmdview-code-block')) {
                wrapper = document.createElement('div');
                wrapper.className = 'editmdview-code-block';
                pre.before(wrapper);
                wrapper.append(pre);
            }

            let tools = wrapper.querySelector(':scope > .editmdview-code-tools');
            if (!tools) {
                tools = document.createElement('div');
                tools.className = 'editmdview-code-tools';
                wrapper.append(tools);
            }

            const labelText = displayLanguage(language);
            let label = tools.querySelector('.editmdview-code-language');
            if (labelText) {
                if (!label) {
                    label = document.createElement('span');
                    label.className = 'editmdview-code-language';
                    tools.prepend(label);
                }
                label.textContent = labelText;
            } else if (label) {
                label.remove();
            }

            if (!tools.querySelector('.editmdview-copy-button')) {
                const button = document.createElement('button');
                button.type = 'button';
                button.className = 'editmdview-copy-button';
                button.title = '复制代码';
                button.setAttribute('aria-label', '复制代码');
                tools.append(button);
            }
        });
    }

    function installCopyFeedback() {
        if (window.__editMdViewCopyFeedbackInstalled) return;
        window.__editMdViewCopyFeedbackInstalled = true;
        window.chrome.webview.addEventListener('message', event => {
            const result = typeof event.data === 'string' ? event.data : '';
            if (!result.startsWith('editmdview-copy-result:')) return;
            const button = window.__editMdViewPendingCopy;
            window.__editMdViewPendingCopy = null;
            if (!button || !button.isConnected) return;
            const copied = result.endsWith(':ok');
            button.classList.toggle('copied', copied);
            button.classList.toggle('failed', !copied);
            button.title = copied ? '已复制' : '复制失败';
            button.setAttribute('aria-label', button.title);
            clearTimeout(button.__editMdViewCopyReset);
            button.__editMdViewCopyReset = setTimeout(() => {
                if (!button.isConnected) return;
                button.classList.remove('copied', 'failed');
                button.title = '复制代码';
                button.setAttribute('aria-label', '复制代码');
            }, 1600);
        });
    }

    function applySourceLines() {
        const meta = document.querySelector('meta[name="editmdview-source-lines"]');
        if (!meta) return;
        const lines = meta.content.split(',').map(Number).filter(Number.isFinite);
        const selector = 'blockquote,ul,ol,li,hr,h1,h2,h3,h4,h5,h6,pre,p,table,thead,tbody,tr,th,td';
        document.querySelectorAll(selector).forEach((node, index) => {
            if (index < lines.length && lines[index] > 0) {
                node.dataset.editmdviewSourceLine = String(lines[index]);
            }
        });
    }

    function installOutline(signal) {
        const outline = document.querySelector('.editmdview-outline');
        if (!outline) return;
        const links = Array.from(outline.querySelectorAll('a[href^="#"]'));
        const entries = links.map(link => {
            let id = (link.getAttribute('href') || '').slice(1);
            try { id = decodeURIComponent(id); } catch (error) { /* Keep the encoded id. */ }
            return { link, heading: id ? document.getElementById(id) : null };
        }).filter(entry => entry.heading);
        let frame = 0;
        const refresh = () => {
            frame = 0;
            const threshold = Math.max(56, window.innerHeight * .24);
            let current = entries.length ? entries[0] : null;
            for (const entry of entries) {
                if (entry.heading.getBoundingClientRect().top <= threshold) current = entry;
                else break;
            }
            for (const entry of entries) {
                const active = entry === current;
                entry.link.classList.toggle('current', active);
                if (active) entry.link.setAttribute('aria-current', 'location');
                else entry.link.removeAttribute('aria-current');
            }
        };
        const schedule = () => { if (!frame) frame = requestAnimationFrame(refresh); };
        addEventListener('scroll', schedule, { passive: true, signal });
        addEventListener('resize', schedule, { passive: true, signal });
        refresh();
    }

    function installWidthHandles(signal) {
        const content = document.querySelector('.editmdview-content');
        const handles = Array.from(document.querySelectorAll('.editmdview-width-handle'));
        if (!content || !handles.length) return;
        const root = document.documentElement;
        const saved = /^editmdview-width:([0-9]+(?:\.[0-9]+)?)$/.exec(window.name || '');
        let preferredWidth = saved ? Number(saved[1]) : 980;
        if (!Number.isFinite(preferredWidth)) preferredWidth = 980;
        const maximumWidth = () => Math.max(320, window.innerWidth - 64);
        const minimumWidth = () => Math.min(560, maximumWidth());
        const appliedWidth = () => Math.max(minimumWidth(), Math.min(maximumWidth(), preferredWidth));
        const applyWidth = () => root.style.setProperty('--editmdview-content-width', appliedWidth() + 'px');
        const persistWidth = () => {
            try { window.name = 'editmdview-width:' + preferredWidth.toFixed(2); } catch (error) { /* Ignore. */ }
        };
        applyWidth();
        addEventListener('resize', applyWidth, { passive: true, signal });
        for (const handle of handles) {
            handle.addEventListener('pointermove', event => {
                handle.style.setProperty('--editmdview-handle-y', event.clientY + 'px');
            }, { signal });
            handle.addEventListener('pointerdown', event => {
                if (event.button !== 0) return;
                event.preventDefault();
                event.stopPropagation();
                const direction = handle.dataset.side === 'left' ? -1 : 1;
                const startX = event.clientX;
                const startWidth = appliedWidth();
                handle.dataset.dragging = '';
                root.classList.add('editmdview-width-dragging');
                try { handle.setPointerCapture(event.pointerId); } catch (error) { /* Ignore. */ }
                const move = moveEvent => {
                    preferredWidth = Math.max(minimumWidth(), Math.min(maximumWidth(),
                        startWidth + (moveEvent.clientX - startX) * direction * 2));
                    handle.style.setProperty('--editmdview-handle-y', moveEvent.clientY + 'px');
                    applyWidth();
                };
                const finish = finishEvent => {
                    handle.removeEventListener('pointermove', move);
                    handle.removeEventListener('pointerup', finish);
                    handle.removeEventListener('pointercancel', finish);
                    delete handle.dataset.dragging;
                    root.classList.remove('editmdview-width-dragging');
                    try { handle.releasePointerCapture(finishEvent.pointerId); } catch (error) { /* Ignore. */ }
                    persistWidth();
                };
                handle.addEventListener('pointermove', move);
                handle.addEventListener('pointerup', finish);
                handle.addEventListener('pointercancel', finish);
            }, { signal });
        }
    }

    function installScrollReporting(signal) {
        let frame = 0;
        const report = () => {
            frame = 0;
            const root = document.documentElement;
            const body = document.body;
            const height = Math.max(root ? root.scrollHeight : 0, body ? body.scrollHeight : 0);
            const maximum = Math.max(0, height - window.innerHeight);
            const fraction = maximum > 0 ? Math.max(0, Math.min(1, window.scrollY / maximum)) : 0;
            try { window.chrome.webview.postMessage('editmdview-scroll:' + fraction.toFixed(8)); } catch (error) { /* Ignore. */ }
        };
        const schedule = () => { if (!frame) frame = requestAnimationFrame(report); };
        addEventListener('scroll', schedule, { passive: true, signal });
        addEventListener('resize', schedule, { passive: true, signal });
    }

    function installClicks(signal) {
        let pointerDown = null;
        document.addEventListener('pointerdown', event => {
            if (event.button === 0) pointerDown = { x: event.clientX, y: event.clientY };
        }, { capture: true, signal });
        document.addEventListener('click', event => {
            if (event.button !== 0 || event.ctrlKey || event.shiftKey || event.altKey || event.metaKey) return;
            const target = event.target instanceof Element ? event.target : null;
            const copyButton = target ? target.closest('.editmdview-copy-button') : null;
            if (copyButton) {
                event.preventDefault();
                event.stopPropagation();
                const wrapper = copyButton.closest('.editmdview-code-block');
                const pre = wrapper ? wrapper.querySelector('pre') : null;
                if (!pre) return;
                window.__editMdViewPendingCopy = copyButton;
                try {
                    window.chrome.webview.postMessage('editmdview-copy-code:' + pre.textContent);
                } catch (error) {
                    copyButton.classList.add('failed');
                    copyButton.title = '复制失败';
                    copyButton.setAttribute('aria-label', '复制失败');
                }
                return;
            }

            const anchor = target ? target.closest('a[href]') : null;
            const href = anchor ? anchor.getAttribute('href') || '' : '';
            if (href.startsWith('#')) {
                event.preventDefault();
                event.stopPropagation();
                let id = href.slice(1);
                try { id = decodeURIComponent(id); } catch (error) { /* Keep encoded id. */ }
                const destination = id ? document.getElementById(id) : document.body;
                if (destination) {
                    destination.scrollIntoView({ behavior: 'smooth', block: 'start' });
                    try { history.replaceState(null, '', id ? '#' + encodeURIComponent(id) : '#'); } catch (error) { /* Ignore. */ }
                }
                return;
            }
            if (target && target.closest('.editmdview-outline,.editmdview-width-handle')) return;
            if (!window.__editMdViewSourceNavigationEnabled) return;
            if (pointerDown && Math.hypot(event.clientX - pointerDown.x, event.clientY - pointerDown.y) > 5) return;
            event.preventDefault();
            event.stopPropagation();
            const mapped = target ? target.closest('[data-editmdview-source-line]') : null;
            if (mapped) {
                window.location.href = 'editmdview://locate/line/' + mapped.dataset.editmdviewSourceLine;
                return;
            }
            const root = document.documentElement;
            const body = document.body;
            const height = Math.max(root ? root.scrollHeight : 0, body ? body.scrollHeight : 0, 1);
            const fraction = Math.max(0, Math.min(1000000,
                Math.round((window.scrollY + event.clientY) / height * 1000000)));
            window.location.href = 'editmdview://locate/fraction/' + fraction;
        }, { capture: true, signal });
    }

    window.EditMdViewPreview = {
        install(options) {
            window.__editMdViewSourceNavigationEnabled = Boolean(options && options.sourceNavigationEnabled);
            try {
                if (window.__editMdViewPreviewAbort) window.__editMdViewPreviewAbort.abort();
            } catch (error) { /* Ignore. */ }
            const controller = new AbortController();
            window.__editMdViewPreviewAbort = controller;
            installCodeBlocks();
            installCopyFeedback();
            applySourceLines();
            installOutline(controller.signal);
            installWidthHandles(controller.signal);
            installScrollReporting(controller.signal);
            installClicks(controller.signal);
            return true;
        },
        find: findInPreview,
        clearFind: clearFindHighlights
    };
}());
