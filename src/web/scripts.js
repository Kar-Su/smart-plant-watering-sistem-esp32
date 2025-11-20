const LIGHT_THRESHOLD = 1500;
const DISCONNECT_TIMEOUT_MS = 60_000;

let autoEnabled = true;
let autoPending = false;

let lastSeenMs = null;

function setDisconnectedUI() {
  const statusText = document.getElementById("status-text");
  const statusDot = document.getElementById("status-dot");
  const waterBadge = document.getElementById("water-badge");

  statusText.textContent = "ESP32 disconnected";
  statusDot.className = "status-dot err";
  waterBadge.textContent = "Pompa: unknown";
  waterBadge.className = "badge badge-idle";

  document.getElementById("btn-water").disabled = true;
}

function setConnectedUIEnableButtons() {
  document.getElementById("btn-water").disabled = false;
}

function checkConnectionStale() {
  if (lastSeenMs === null) return;

  const age = Date.now() - lastSeenMs;
  if (age > DISCONNECT_TIMEOUT_MS) {
    setDisconnectedUI();
  }
}

async function fetchLatest() {
  try {
    const res = await fetch("/api/latest", { cache: "no-store" });
    if (!res.ok) throw new Error("HTTP " + res.status);

    const data = await res.json();

    const soilEl = document.getElementById("soil-value");
    const lightEl = document.getElementById("light-value");
    const statusText = document.getElementById("status-text");
    const statusDot = document.getElementById("status-dot");
    const lastUpdate = document.getElementById("last-update");
    const waterBadge = document.getElementById("water-badge");
    const warningBar = document.getElementById("light-warning");
    const autoToggle = document.getElementById("auto-toggle");
    const autoLabel = document.getElementById("auto-label");

    if (data === null) {
      statusText.textContent = "Belum ada data dari ESP32";
      statusDot.className = "status-dot wait";
      soilEl.textContent = "-";
      lightEl.textContent = "-";
      lastUpdate.textContent = "Last update: -";
      waterBadge.textContent = "Pompa OFF";
      waterBadge.className = "badge badge-idle";
      warningBar.classList.add("hidden");
      return;
    }

    const updated = new Date(data.updated_at);
    lastSeenMs = updated.getTime();

    if (Date.now() - lastSeenMs > DISCONNECT_TIMEOUT_MS) {
      setDisconnectedUI();
      return;
    } else {
      setConnectedUIEnableButtons();
    }

    soilEl.textContent = data.soil;
    lightEl.textContent = data.light;

    if (data.light < LIGHT_THRESHOLD) warningBar.classList.remove("hidden");
    else warningBar.classList.add("hidden");

    if (data.is_watering === true) {
      statusText.textContent = "ESP32 sedang menyiram";
      statusDot.className = "status-dot ok";
      waterBadge.textContent = "Pompa ON";
      waterBadge.className = "badge badge-watering";
    } else {
      statusText.textContent = "ESP32 sedang monitoring";
      statusDot.className = "status-dot ok";
      waterBadge.textContent = "Pompa OFF";
      waterBadge.className = "badge badge-idle";
    }

    if (typeof data.auto_enabled === "boolean") {
      if (!autoPending) {
        autoEnabled = data.auto_enabled;
      } else if (data.auto_enabled === autoEnabled) {
        autoPending = false;
      }
    }

    autoToggle.checked = autoEnabled;
    autoLabel.textContent = autoEnabled ? "Mode auto: ON" : "Mode auto: OFF";

    lastUpdate.textContent = "Last update: " + updated.toLocaleTimeString();
  } catch (err) {
    console.error("fetchLatest error:", err);
  }
}

const btnWater = document.getElementById("btn-water");
const waterStatus = document.getElementById("water-status");
const autoToggle = document.getElementById("auto-toggle");
const autoLabel = document.getElementById("auto-label");

btnWater.addEventListener("click", async () => {
  btnWater.disabled = true;
  waterStatus.textContent = "Mengirim perintah siram...";
  try {
    const res = await fetch("/api/water", { method: "POST" });
    if (!res.ok) throw new Error("HTTP " + res.status);
    waterStatus.textContent = "Perintah siram dikirim.";
  } catch (err) {
    waterStatus.textContent = "Gagal mengirim perintah.";
  } finally {
    setTimeout(() => {
      btnWater.disabled = false;
      waterStatus.textContent = "";
    }, 3000);
  }
});

autoToggle.addEventListener("change", async () => {
  autoEnabled = autoToggle.checked;
  autoPending = true;
  autoLabel.textContent = autoEnabled ? "Mode auto: ON" : "Mode auto: OFF";

  try {
    await fetch("/api/auto", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ enabled: autoEnabled }),
    });
  } catch (err) {
    autoPending = false;
    autoEnabled = !autoEnabled;
    autoToggle.checked = autoEnabled;
    autoLabel.textContent = autoEnabled ? "Mode auto: ON" : "Mode auto: OFF";
  }
});

fetchLatest();
setInterval(fetchLatest, 2000);

setInterval(checkConnectionStale, 1000);
