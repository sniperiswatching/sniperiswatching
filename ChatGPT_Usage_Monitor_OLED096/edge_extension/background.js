chrome.runtime.onMessage.addListener((message, sender) => {
  if (message?.type !== "usage_snapshot" || !sender?.tab?.url) return;
  if (!/^https:\/\/(chatgpt\.com|chat\.openai\.com)\//.test(sender.tab.url)) return;

  fetch("http://127.0.0.1:8765/usage", {
    method: "POST",
    headers: {"Content-Type": "application/json"},
    body: JSON.stringify(message.payload)
  }).catch(error => console.warn("ChatGPT usage bridge unavailable", error));
});

