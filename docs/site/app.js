/* VibeStudio documentation: theme, search, copy buttons, navigation and the
   "on this page" highlight. No dependencies; works from file:// as well as the web. */
(function () {
  "use strict";

  var root = document.documentElement;
  var STORAGE_KEY = "vibestudio-docs-theme";

  /* ---------------------------------------------------------------- theme */
  var themeButton = document.querySelector(".theme-toggle");
  var themeLabels = { auto: "automatic", light: "light", dark: "dark" };

  function storedTheme() {
    try { return localStorage.getItem(STORAGE_KEY) || "auto"; } catch (e) { return "auto"; }
  }
  function applyTheme(theme) {
    root.setAttribute("data-theme", theme);
    if (themeButton) themeButton.setAttribute("aria-label", "Colour theme: " + themeLabels[theme] + ". Change theme");
    try {
      if (theme === "auto") localStorage.removeItem(STORAGE_KEY); else localStorage.setItem(STORAGE_KEY, theme);
    } catch (e) { /* storage may be unavailable */ }
  }
  applyTheme(storedTheme());
  if (themeButton) {
    themeButton.addEventListener("click", function () {
      var order = ["auto", "light", "dark"];
      var next = order[(order.indexOf(root.getAttribute("data-theme")) + 1) % order.length];
      applyTheme(next);
    });
  }

  /* ------------------------------------------------------------ navigation */
  var navToggle = document.querySelector(".nav-toggle");
  var sidebar = document.getElementById("sidebar");
  function setNav(open) {
    document.body.classList.toggle("nav-open", open);
    if (navToggle) {
      navToggle.setAttribute("aria-expanded", open ? "true" : "false");
      navToggle.setAttribute("aria-label", open ? "Hide navigation" : "Show navigation");
    }
  }
  if (navToggle && sidebar) {
    navToggle.addEventListener("click", function () { setNav(!document.body.classList.contains("nav-open")); });
    sidebar.addEventListener("click", function (event) { if (event.target.closest("a")) setNav(false); });
    document.addEventListener("keydown", function (event) {
      if (event.key === "Escape" && document.body.classList.contains("nav-open")) { setNav(false); navToggle.focus(); }
    });
    var current = sidebar.querySelector('[aria-current="page"]');
    if (current && current.scrollIntoView) current.scrollIntoView({ block: "center" });
  }

  /* ---------------------------------------------------------- copy buttons */
  document.querySelectorAll(".article pre").forEach(function (pre) {
    var host = pre.closest(".highlight") || pre;
    var wrapper = document.createElement("div");
    wrapper.className = "code-block";
    host.parentNode.insertBefore(wrapper, host);
    wrapper.appendChild(host);
    if (!navigator.clipboard) return;
    var button = document.createElement("button");
    button.type = "button";
    button.className = "copy-button";
    button.textContent = "Copy";
    button.setAttribute("aria-label", "Copy code to clipboard");
    button.addEventListener("click", function () {
      navigator.clipboard.writeText(pre.innerText.replace(/\n$/, "")).then(function () {
        button.textContent = "Copied";
        button.classList.add("copied");
        setTimeout(function () { button.textContent = "Copy"; button.classList.remove("copied"); }, 1600);
      });
    });
    wrapper.appendChild(button);
  });

  /* ----------------------------------------------------- tables scroll */
  document.querySelectorAll(".article table").forEach(function (table) {
    if (table.parentElement.classList.contains("table-wrap")) return;
    var wrap = document.createElement("div");
    wrap.className = "table-wrap";
    table.parentNode.insertBefore(wrap, table);
    wrap.appendChild(table);
  });

  /* ------------------------------------------------- on-this-page highlight */
  var tocLinks = Array.prototype.slice.call(document.querySelectorAll(".toc a"));
  if (tocLinks.length && "IntersectionObserver" in window) {
    var byId = {};
    tocLinks.forEach(function (link) { byId[decodeURIComponent(link.hash.slice(1))] = link; });
    var headings = tocLinks.map(function (link) { return document.getElementById(decodeURIComponent(link.hash.slice(1))); })
      .filter(Boolean);
    var visible = new Set();
    var observer = new IntersectionObserver(function (entries) {
      entries.forEach(function (entry) { if (entry.isIntersecting) visible.add(entry.target.id); else visible.delete(entry.target.id); });
      var active = headings.find(function (h) { return visible.has(h.id); });
      if (!active) return;
      tocLinks.forEach(function (link) { link.classList.remove("active"); });
      if (byId[active.id]) byId[active.id].classList.add("active");
    }, { rootMargin: "-64px 0px -65% 0px" });
    headings.forEach(function (h) { observer.observe(h); });
  }

  /* ---------------------------------------------------------------- search */
  var input = document.getElementById("search-input");
  var results = document.getElementById("search-results");
  var index = window.VIBESTUDIO_SEARCH || [];
  var siteRoot = (document.querySelector('link[rel="stylesheet"]').getAttribute("href") || "").replace(/assets\/style\.css$/, "");
  var selected = -1;

  function escapeHtml(text) {
    return text.replace(/[&<>"']/g, function (c) { return { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]; });
  }
  function highlight(text, terms) {
    var html = escapeHtml(text);
    terms.forEach(function (term) {
      if (term.length < 2) return;
      html = html.replace(new RegExp("(" + term.replace(/[.*+?^${}()|[\]\\]/g, "\\$&") + ")", "gi"), "<mark>$1</mark>");
    });
    return html;
  }
  function snippet(text, terms) {
    var lower = text.toLowerCase();
    var at = -1;
    terms.some(function (term) { at = lower.indexOf(term); return at >= 0; });
    var start = Math.max(0, at - 50);
    var piece = text.slice(start, start + 160);
    return (start > 0 ? "…" : "") + piece + (start + 160 < text.length ? "…" : "");
  }
  function search(query) {
    var terms = query.toLowerCase().split(/\s+/).filter(Boolean);
    if (!terms.length) return [];
    var scored = [];
    index.forEach(function (entry) {
      var title = entry.h.toLowerCase();
      var page = entry.t.toLowerCase();
      var body = entry.x.toLowerCase();
      var score = 0;
      for (var i = 0; i < terms.length; i++) {
        var term = terms[i];
        var inTitle = title.indexOf(term) >= 0;
        var inPage = page.indexOf(term) >= 0;
        var inBody = body.indexOf(term) >= 0;
        if (!inTitle && !inPage && !inBody) return;
        score += (inTitle ? 10 : 0) + (inPage ? 4 : 0) + (inBody ? 1 : 0);
        if (title.indexOf(term) === 0) score += 4;
      }
      if (title === query.toLowerCase()) score += 20;
      scored.push({ entry: entry, score: score });
    });
    scored.sort(function (a, b) { return b.score - a.score; });
    return scored.slice(0, 12).map(function (item) { return { entry: item.entry, terms: terms }; });
  }
  function render(query) {
    var found = search(query);
    selected = -1;
    if (!query.trim()) { close(); return; }
    if (!found.length) {
      results.innerHTML = '<li class="empty" role="option" aria-disabled="true">No results for “' + escapeHtml(query) + '”</li>';
    } else {
      results.innerHTML = found.map(function (item, i) {
        var e = item.entry;
        var heading = e.h === e.t ? e.t : e.h;
        return '<li role="none"><a role="option" id="result-' + i + '" aria-selected="false" href="' + siteRoot + e.u + '">' +
          '<span class="result-title">' + highlight(heading, item.terms) + '</span> ' +
          (e.h === e.t ? "" : '<span class="result-page">' + escapeHtml(e.t) + "</span>") +
          '<span class="result-snippet">' + highlight(snippet(e.x, item.terms), item.terms) + "</span></a></li>";
      }).join("");
    }
    results.hidden = false;
    input.setAttribute("aria-expanded", "true");
  }
  function close() {
    results.hidden = true;
    results.innerHTML = "";
    input.setAttribute("aria-expanded", "false");
    input.removeAttribute("aria-activedescendant");
  }
  function move(step) {
    var options = results.querySelectorAll('a[role="option"]');
    if (!options.length) return;
    if (selected >= 0) options[selected].setAttribute("aria-selected", "false");
    selected = (selected + step + options.length) % options.length;
    options[selected].setAttribute("aria-selected", "true");
    input.setAttribute("aria-activedescendant", options[selected].id);
    options[selected].scrollIntoView({ block: "nearest" });
  }
  if (input && results) {
    input.addEventListener("input", function () { render(input.value); });
    input.addEventListener("keydown", function (event) {
      if (event.key === "ArrowDown") { event.preventDefault(); move(1); }
      else if (event.key === "ArrowUp") { event.preventDefault(); move(-1); }
      else if (event.key === "Enter") {
        var options = results.querySelectorAll('a[role="option"]');
        var target = options[selected >= 0 ? selected : 0];
        if (target) { event.preventDefault(); window.location.href = target.href; }
      } else if (event.key === "Escape") { input.value = ""; close(); input.blur(); }
    });
    input.addEventListener("blur", function () { setTimeout(function () { if (!results.contains(document.activeElement)) close(); }, 150); });
    document.addEventListener("keydown", function (event) {
      var typing = /^(input|textarea|select)$/i.test(document.activeElement.tagName) || document.activeElement.isContentEditable;
      if (event.key === "/" && !typing) { event.preventDefault(); input.focus(); input.select(); }
    });
  }
})();
