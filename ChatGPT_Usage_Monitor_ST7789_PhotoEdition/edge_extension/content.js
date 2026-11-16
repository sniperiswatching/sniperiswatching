// Read only text currently visible in the signed-in ChatGPT tab.
// Credentials, cookies, tokens, and page HTML are never sent to the bridge.
(() => {
  const fiveHourHeading = /5\s*小时限额/;
  const weeklyHeading = /每周限额/;
  let lastPayload = "";
  let lastSentAt = 0;

  function usageContainerText() {
    const elements = [...document.querySelectorAll("main,section,article,div")];
    const candidates = [];
    for (const element of elements) {
      const text = (element.innerText || "").replace(/\u00a0/g, " ").trim();
      if (!text || text.length > 12000) continue;
      if (!fiveHourHeading.test(text) || !weeklyHeading.test(text)) continue;
      if ((text.match(/剩余\s*\d{1,3}\s*%/g) || []).length < 2) continue;
      candidates.push(text);
    }
    candidates.sort((left, right) => left.length - right.length);
    return candidates[0] || document.body.innerText || "";
  }

  function parseQuotaBlock(text, heading, otherHeading) {
    const start = text.search(heading);
    if (start < 0) return null;
    let section = text.slice(start);
    const end = section.slice(1).search(otherHeading);
    if (end >= 0) section = section.slice(0, end + 1);

    const percentage = section.match(/剩余\s*(\d{1,3})\s*%/);
    if (!percentage) return null;
    const remaining = Number(percentage[1]);
    if (!Number.isInteger(remaining) || remaining < 0 || remaining > 100) return null;

    const reset = section.match(/((?:约\s*)?[\d\s]*(?:天|小时|分钟)[\d\s天小时分钟]*)\s*后重置/);
    return {
      remaining,
      reset: reset ? reset[1].replace(/\s+/g, "").replace(/^约/, "约") : "--"
    };
  }

  function collect() {
    const text = usageContainerText();
    if (!text) return;
    const fiveHour = parseQuotaBlock(text, fiveHourHeading, weeklyHeading);
    const weekly = parseQuotaBlock(text, weeklyHeading, fiveHourHeading);
    if (!fiveHour || !weekly) return;

    const payload = {
      five_hour_remaining_pct: fiveHour.remaining,
      weekly_remaining_pct: weekly.remaining,
      five_hour_reset: fiveHour.reset,
      weekly_reset: weekly.reset
    };
    const serialized = JSON.stringify(payload);
    const now = Date.now();
    if (serialized === lastPayload && now - lastSentAt < 55000) return;
    lastPayload = serialized;
    lastSentAt = now;
    chrome.runtime.sendMessage({type: "usage_snapshot", payload});
  }

  collect();
  setInterval(collect, 30000);
  let mutationTimer = 0;
  const observer = new MutationObserver(() => {
    clearTimeout(mutationTimer);
    mutationTimer = setTimeout(collect, 1200);
  });
  observer.observe(document.documentElement, {subtree: true, childList: true, characterData: true});
})();
