document.addEventListener('DOMContentLoaded', () => {
    fetchStatus();
    loadAnimals();

    // Auto-refresh status
    setInterval(fetchStatus, 5000);

    // Form Handler
    document.getElementById('add-form').addEventListener('submit', async (e) => {
        e.preventDefault();
        const name = document.getElementById('name').value;
        const species = document.getElementById('species').value;

        try {
            const res = await fetch('/api/animals', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ name, species })
            });
            if (res.ok) {
                document.getElementById('add-form').reset();
                loadAnimals();
            } else {
                alert('Failed to add animal');
            }
        } catch (err) {
            console.error(err);
        }
    });

    document.getElementById('refresh-btn').onclick = loadAnimals;
});

async function fetchStatus() {
    try {
        const res = await fetch('/health');
        const data = await res.json();

        document.getElementById('uptime').textContent = formatUptime(data.uptime);
        document.getElementById('heap').textContent = `${Math.round(data.heap.free / 1024)} KB`;
        document.getElementById('storage').textContent = data.storage.mounted ?
            `${data.storage.free_kb} KB Free` : 'Unmounted';

        const wifiStatus = document.getElementById('wifi-status');
        wifiStatus.textContent = data.wifi.connected ? `Signal: ${data.wifi.rssi}dBm` : 'Disconnected';
        wifiStatus.style.color = data.wifi.connected ? 'green' : 'red';
        document.getElementById('ip-addr').textContent = data.wifi.ip;
    } catch (e) {
        console.log('Status offline');
    }
}

async function loadAnimals() {
    try {
        const res = await fetch('/api/animals');
        const animals = await res.json();
        const container = document.getElementById('animal-list');
        container.innerHTML = animals.map(a => `
            <div class="animal-item">
                <strong>${escapeHtml(a.name)}</strong>
                <span>${escapeHtml(a.species)}</span>
                <small>${a.id}</small>
            </div>
        `).join('');
    } catch (e) {
        console.error('Failed to load animals');
    }
}

function escapeHtml(text) {
    if (!text) return '';
    return text
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;")
        .replace(/"/g, "&quot;")
        .replace(/'/g, "&#039;");
}

function formatUptime(sec) {
    const h = Math.floor(sec / 3600);
    const m = Math.floor((sec % 3600) / 60);
    return `${h}h ${m}m`;
}
