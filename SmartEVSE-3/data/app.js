'use strict';

/* ========== DOM helpers ========== */
const $id = (id) => document.getElementById(id);
const $qs = (sel) => document.querySelector(sel);
const $qa = (sel) => document.querySelectorAll(sel);

function showEl(el) { if (el) el.style.display = ''; }
function hideEl(el) { if (el) el.style.display = 'none'; }
function showById(id) { showEl($id(id)); }
function hideById(id) { hideEl($id(id)); }
function showAll(sel) { $qa(sel).forEach(showEl); }
function hideAll(sel) { $qa(sel).forEach(hideEl); }

/* ========== Section collapse ========== */
function toggleSection(el) {
    el.classList.toggle('collapsed');
    var body = el.nextElementSibling;
    if (body) body.classList.toggle('collapsed');
}

/* ========== Slave restriction helper ========== */
var SLAVE_TOOLTIP = 'Only editable on master node';
function setSlaveRestricted(id, restricted) {
    var el = $id(id);
    if (!el) return;
    if (restricted) {
        el.disabled = true;
        el.style.opacity = '0.5';
        el.title = SLAVE_TOOLTIP;
    } else {
        el.disabled = false;
        el.style.opacity = '';
        el.title = '';
    }
}

/* ========== State ========== */
const endpoint = document.location + 'settings';
let initiated = false;
let mqttEditMode = false;
let ocppEditMode = false;
let last_evse_state_id = 0;

/* ========== WebSocket Data Channel ========== */
var dataWs = null;
var dataWsReconnectTimer = null;
var dataWsReconnectAttempts = 0;
var wsConnected = false;
/* Cache of last known WS values for computing totals */
var wsCache = {
    phase_L1: 0, phase_L2: 0, phase_L3: 0,
    evmeter_L1: 0, evmeter_L2: 0, evmeter_L3: 0,
    evmeter_power: 0, battery_current: 0
};

function updateConnStatus(connected) {
    var el = $id('conn_status');
    if (!el) return;
    el.style.backgroundColor = connected ? '#1cc88a' : '#e74a3b';
    el.title = connected ? 'Live (WebSocket)' : 'Polling (HTTP)';
}

/* Apply flat WS data fields to the DOM */
function applyWsData(d) {
    if (d.state_id !== undefined || d.error_flags !== undefined)
        updateStateDot(d.state_id, d.error_flags);
    if (d.mode_id !== undefined) {
        var modeNames = {0:'OFF', 1:'NORMAL', 2:'SOLAR', 3:'SMART', 4:'PAUSE'};
        $qs('#mode').textContent = modeNames[d.mode_id] || 'N/A';
        for (var x of [0, 1, 2, 3, 4]) {
            $qs('#mode_' + x).classList.toggle('active', x === d.mode_id);
        }
        syncMobileNav(d.mode_id);
        if (d.mode_id == 2) {
            showAll('.with_solar');
            hideById('override_current_box');
            hideById('override_current_box2');
        } else {
            hideAll('.with_solar');
            showById('override_current_box');
            showById('override_current_box2');
        }
    }
    if (d.charge_current !== undefined)
        $id('charge_current').textContent = (d.charge_current / 10).toFixed(1) + " A";
    /* Hero power display (big number) */
    if (d.evmeter_power !== undefined) {
        var heroP = $id('evmeter_power');
        if (heroP) {
            var kw = (d.evmeter_power / 1000).toFixed(1);
            heroP.innerHTML = kw + '<span class="power-unit">kW</span>';
        }
    }
    if (d.temp !== undefined) {
        var maxT = d.temp_max !== undefined ? d.temp_max : '';
        if (maxT !== '') $id('temp').textContent = d.temp + " \u00B0C / " + maxT + " \u00B0C";
    }
    if (d.pwm !== undefined)
        $id('dutycycle').textContent = (d.pwm * 100 / 1024).toFixed(0) + " %";
    if (d.car_connected !== undefined)
        $id('car_connected').textContent = d.car_connected ? "Yes" : "No";
    if (d.override_current !== undefined)
        $id('override_current').textContent = (d.override_current / 10).toFixed(1) + " A";
    if (d.current_min !== undefined)
        $id('current_min').textContent = d.current_min + " A";
    if (d.current_max !== undefined)
        $id('current_max').textContent = d.current_max + " A";

    /* Phase currents - update cache and recompute totals */
    var phaseChanged = false;
    if (d.phase_L1 !== undefined) { wsCache.phase_L1 = d.phase_L1; phaseChanged = true; }
    if (d.phase_L2 !== undefined) { wsCache.phase_L2 = d.phase_L2; phaseChanged = true; }
    if (d.phase_L3 !== undefined) { wsCache.phase_L3 = d.phase_L3; phaseChanged = true; }
    if (phaseChanged) {
        $id('phase_1').textContent = (wsCache.phase_L1 / 10).toFixed(1) + " A";
        $id('phase_2').textContent = (wsCache.phase_L2 / 10).toFixed(1) + " A";
        $id('phase_3').textContent = (wsCache.phase_L3 / 10).toFixed(1) + " A";
        $id('phase_total').textContent = ((wsCache.phase_L1 + wsCache.phase_L2 + wsCache.phase_L3) / 10).toFixed(1) + " A";
        updatePhaseBars(wsCache.phase_L1, wsCache.phase_L2, wsCache.phase_L3);
    }

    var evChanged = false;
    if (d.evmeter_L1 !== undefined) { wsCache.evmeter_L1 = d.evmeter_L1; evChanged = true; }
    if (d.evmeter_L2 !== undefined) { wsCache.evmeter_L2 = d.evmeter_L2; evChanged = true; }
    if (d.evmeter_L3 !== undefined) { wsCache.evmeter_L3 = d.evmeter_L3; evChanged = true; }
    if (evChanged) {
        $id('evmeter_currents_1').textContent = (wsCache.evmeter_L1 / 10).toFixed(1) + " A";
        $id('evmeter_currents_2').textContent = (wsCache.evmeter_L2 / 10).toFixed(1) + " A";
        $id('evmeter_currents_3').textContent = (wsCache.evmeter_L3 / 10).toFixed(1) + " A";
        $id('evmeter_currents_total').textContent = ((wsCache.evmeter_L1 + wsCache.evmeter_L2 + wsCache.evmeter_L3) / 10).toFixed(1) + " A";
    }

    if (d.evmeter_power !== undefined) {
        wsCache.evmeter_power = d.evmeter_power;
        /* Hero power updated above via heroP block */
    }
    if (d.evmeter_charged_wh !== undefined)
        $id('evmeter_charged_kwh').textContent = (d.evmeter_charged_wh / 1000).toFixed(1) + " kWh";
    /* Update power flow when any relevant value changes */
    if (phaseChanged || d.evmeter_power !== undefined || d.battery_current !== undefined) {
        var mt = wsCache.phase_L1 + wsCache.phase_L2 + wsCache.phase_L3;
        updatePowerFlow(mt, wsCache.evmeter_power || 0, d.battery_current !== undefined ? d.battery_current : (wsCache.battery_current || 0));
    }
    if (d.battery_current !== undefined) {
        wsCache.battery_current = d.battery_current;
        $id('battery_current').textContent = (d.battery_current / 10).toFixed(1) + " A";
        if (d.battery_current == 0) $id('battery_status').textContent = "Idle";
        else $id('battery_status').textContent = d.battery_current < 0 ? "Discharging" : "Charging";
    }
    if (d.solar_stop_timer !== undefined && d.solar_stop_timer > 0) {
        var stateEl = $id('state');
        if (stateEl && stateEl.textContent.indexOf('Stopping') === -1)
            stateEl.textContent += " (Stopping in " + d.solar_stop_timer + "s)";
    }
    if (d.loadbl !== undefined) {
        if (d.loadbl > 1) {
            showById('loadbl'); showById('loadbl_text');
            $id('loadbl_node').textContent = "Slave Node " + (d.loadbl - 1);
            hideById('contactor2');
        } else if (d.loadbl == 1) {
            showById('loadbl'); showById('loadbl_text');
            $id('loadbl_node').textContent = "Master";
            hideById('contactor2');
        } else {
            hideById('loadbl'); hideById('loadbl_text');
            showById('contactor2');
        }
    }
}

function connectDataWs() {
    if (dataWs && (dataWs.readyState === WebSocket.OPEN || dataWs.readyState === WebSocket.CONNECTING)) return;
    var wsUrl = (window.location.protocol === 'https:' ? 'wss:' : 'ws:') + '//' + window.location.host + '/ws/data';
    var socket = new WebSocket(wsUrl);
    dataWs = socket;

    socket.onopen = function() {
        dataWsReconnectAttempts = 0;
        wsConnected = true;
        updateConnStatus(true);
        socket.send(JSON.stringify({subscribe: ['state']}));
    };

    socket.onmessage = function(event) {
        try {
            var msg = JSON.parse(event.data);
            if (msg.d) applyWsData(msg.d);
        } catch(e) { /* ignore parse errors */ }
    };

    socket.onerror = function() {
        if (socket.readyState !== WebSocket.CLOSED) socket.close();
    };

    socket.onclose = function() {
        if (dataWs === socket) dataWs = null;
        wsConnected = false;
        updateConnStatus(false);
        var delay = Math.min(1000 * Math.pow(2, dataWsReconnectAttempts), 10000) + Math.floor(Math.random() * 500);
        dataWsReconnectAttempts++;
        dataWsReconnectTimer = setTimeout(connectDataWs, delay);
        /* Resume polling while WS is disconnected */
        loadData();
    };
}

/* Visibility-aware pause/resume for data WS (registered once) */
document.addEventListener('visibilitychange', function() {
    if (document.hidden) {
        if (dataWs) dataWs.close();
    } else if (!dataWs || dataWs.readyState !== WebSocket.OPEN) {
        clearTimeout(dataWsReconnectTimer);
        dataWsReconnectAttempts = 0;
        connectDataWs();
    }
});

/* ========== Theme (dark mode) ========== */
function getTheme() {
    var stored = localStorage.getItem('evse_theme');
    if (stored) return stored;
    return window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
}
function applyTheme(theme) {
    document.documentElement.setAttribute('data-theme', theme);
    var btn = $id('theme_toggle');
    if (btn) btn.textContent = theme === 'dark' ? '\u2600' : '\u263E'; /* sun or moon */
}
function toggleTheme() {
    var current = document.documentElement.getAttribute('data-theme') || getTheme();
    var next = current === 'dark' ? 'light' : 'dark';
    localStorage.setItem('evse_theme', next);
    applyTheme(next);
}
/* Apply saved theme immediately */
applyTheme(getTheme());

/* ========== UI helpers ========== */
var maxMainsAmps = 25; /* default, updated on loadData */

function updateStateDot(stateId, errorFlags) {
    var dot = $id('state_dot');
    if (!dot) return;
    if (errorFlags && errorFlags > 0) { dot.style.backgroundColor = '#e74a3b'; return; }
    /* stateId: 0=A(idle), 1=B(connected), 2=C(charging), 8=Activate, etc. */
    if (stateId === 2 || stateId === 6 || stateId === 7) dot.style.backgroundColor = '#1cc88a'; /* charging - green */
    else if (stateId === 1 || stateId === 5 || stateId === 9) dot.style.backgroundColor = '#36b9cc'; /* connected - blue */
    else dot.style.backgroundColor = '#858796'; /* idle/off - gray */
}

function updatePhaseBars(l1, l2, l3, prefix) {
    prefix = prefix || 'mains_bar';
    var max = maxMainsAmps * 10; /* values are in 0.1A */
    var vals = [l1, l2, l3];
    var labels = ['L1', 'L2', 'L3'];
    for (var i = 0; i < 3; i++) {
        var pct = max > 0 ? Math.min(100, Math.abs(vals[i]) / max * 100) : 0;
        var barEl = $id(prefix + '_' + labels[i]);
        var valEl = $id(prefix + '_val_' + labels[i]);
        if (barEl) barEl.style.width = pct.toFixed(1) + '%';
        if (valEl) valEl.textContent = (vals[i] / 10).toFixed(1) + 'A';
    }
}

function updateNodeOverview(nodes, maxCurrent) {
    var container = $id('node_list');
    if (!container || !nodes) return;
    var html = '';
    var totalCurrent = 0;
    var activeCount = 0;
    for (var i = 0; i < nodes.length; i++) {
        var n = nodes[i];
        if (n.state === 'Idle' && n.current === 0 && i > 0) continue; /* skip unused nodes */
        activeCount++;
        totalCurrent += n.current;
        var pct = maxCurrent > 0 ? Math.min(100, n.current / maxCurrent * 100) : 0;
        var color = n.state === 'Charging' ? '#1cc88a' : n.state === 'Request' ? '#f6c23e' : '#858796';
        var label = i === 0 ? 'Master' : 'Node ' + i;
        var stColor = n.state === 'Charging' ? '#1cc88a' : n.state === 'Request' ? '#f6c23e' : '#858796';
        var stateBadge = '<span style="font-size:.65rem;padding:1px 4px;border-radius:3px;background:' +
            stColor + ';color:#fff;margin-left:4px;white-space:nowrap;">' + n.state + '</span>';
        html += '<div class="phase-bar-row" style="margin-bottom:4px;">' +
            '<span class="phase-bar-label" style="width:60px;flex-shrink:0;">' + label + '</span>' +
            '<div class="phase-bar-track"><div style="height:100%;border-radius:5px;width:' +
            pct.toFixed(1) + '%;background:' + color + ';transition:width .4s;min-width:2px;"></div></div>' +
            '<span class="phase-bar-value" style="width:auto;min-width:55px;white-space:nowrap;">' + (n.current / 10).toFixed(1) + 'A' + stateBadge + '</span></div>';
    }
    container.innerHTML = html;
    /* Total bar */
    var totalPct = maxCurrent > 0 ? Math.min(100, totalCurrent / (maxCurrent * activeCount) * 100) : 0;
    var totalBar = $id('lb_total_bar');
    var totalVal = $id('lb_total_val');
    if (totalBar) totalBar.style.width = totalPct.toFixed(1) + '%';
    if (totalVal) totalVal.textContent = (totalCurrent / 10).toFixed(1) + 'A';
}

function syncMobileNav(modeId) {
    for (var x of [0, 1, 2, 3, 4]) {
        var btn = $id('mnav_' + x);
        if (btn) btn.classList.toggle('active', x === modeId);
    }
}

/* ========== Power flow diagram ========== */
function updatePowerFlow(mainsTotal, evPower, batCurrent) {
    /* mainsTotal in 0.1A, evPower in W, batCurrent in 0.1A */
    var lineGH = $id('pf_line_gh');
    var lineHE = $id('pf_line_he');
    var lineBH = $id('pf_line_bh');
    var gridVal = $id('pf_grid_val');
    var evseVal = $id('pf_evse_val');
    var batGroup = $id('pf_battery');
    var batVal = $id('pf_bat_val');

    if (!lineGH) return;

    /* Grid -> Home flow */
    var gridW = Math.abs(mainsTotal) * 23; /* rough W estimate at 230V */
    if (gridVal) gridVal.textContent = (gridW / 1000).toFixed(1) + ' kW';
    var sw = Math.max(2, Math.min(6, Math.abs(mainsTotal) / 100));
    lineGH.style.strokeWidth = sw;
    if (mainsTotal > 5) {
        lineGH.setAttribute('class', 'pf-line flow-fwd'); /* importing */
    } else if (mainsTotal < -5) {
        lineGH.setAttribute('class', 'pf-line flow-rev'); /* exporting */
    } else {
        lineGH.setAttribute('class', 'pf-line flow-none');
    }

    /* Home -> EVSE flow */
    if (evseVal) evseVal.textContent = (Math.abs(evPower) / 1000).toFixed(1) + ' kW';
    var sw2 = Math.max(2, Math.min(6, Math.abs(evPower) / 2000));
    lineHE.style.strokeWidth = sw2;
    if (evPower > 50) {
        lineHE.setAttribute('class', 'pf-line flow-fwd');
    } else {
        lineHE.setAttribute('class', 'pf-line flow-none');
    }

    /* Battery (optional) */
    if (batCurrent !== undefined && batCurrent !== 0) {
        if (batGroup) batGroup.style.display = '';
        if (lineBH) lineBH.style.display = '';
        if (batVal) batVal.textContent = (Math.abs(batCurrent) / 10).toFixed(1) + ' A';
        if (lineBH) {
            if (batCurrent > 0) {
                lineBH.setAttribute('class', 'pf-line flow-fwd'); /* charging battery */
            } else {
                lineBH.setAttribute('class', 'pf-line flow-rev'); /* discharging */
            }
        }
    }
}

/* ========== Capacity Tariff ========== */
function updateCapacityTariff(limit, windowAvg, monthlyPeak, headroom) {
    var el = $id('capacity_limit_input');
    if (el && !el.matches(':focus')) el.value = limit > 0 ? (limit / 1000).toFixed(1) : '0';
    var fmtW = function(w) { return w !== undefined && w !== null ? (w / 1000).toFixed(1) + ' kW' : '-'; };
    $id('capacity_window_avg').textContent = fmtW(windowAvg);
    $id('capacity_monthly_peak').textContent = fmtW(monthlyPeak);
    $id('capacity_headroom').textContent = limit > 0 ? fmtW(headroom) : 'N/A';
    if (limit > 0) showById('capacity_tariff_card'); else showById('capacity_tariff_card');
}
function setCapacityLimit() {
    var val = parseFloat($id('capacity_limit_input').value);
    if (isNaN(val) || val < 0 || val > 25) { alert('Value must be 0-25 kW'); return; }
    var watts = Math.round(val * 1000);
    fetch('/settings?capacity_limit=' + watts, { method: 'POST' , body: '' });
}

/* ========== Cert visibility ========== */
function toggleCertVisibility() {
    $id('mqtt_ca_cert_wrapper').style.display =
        $id('mqtt_tls').checked ? '' : 'none';
}

/* ========== Data loading ========== */
function loadData() {
    fetch(endpoint)
        .then(function(r) { return r.json(); })
        .then(function(data) {
            if (!initiated) {
                initiated = true;
                var versionEl = $id('version');
                versionEl.textContent = data.version;
                versionEl.dataset.version = data.version;
                sessionStorage.setItem("version", JSON.stringify(data.version));

                $id('serialnr').textContent += data.serialnr;
                sessionStorage.setItem("serialnr", JSON.stringify(data.serialnr));

                var minCurrent = parseInt(data.settings.current_min.toFixed(1));
                var maxCurrent = parseInt(data.settings.current_max.toFixed(1));

                if (data.evse.loadbl < 2) {
                    var select = $id('mode_override_current');
                    select.add(new Option('no override', 0));
                    for (var x = minCurrent; x <= maxCurrent; x++) {
                        select.add(new Option(x + 'A', x));
                    }
                }
                $id('required_evccid').value = data.settings.required_evccid || "";
            }

            /* Mode display */
            $qs('#mode').textContent = data.mode;
            for (var x of [0, 1, 2, 3, 4]) {
                $qs('#mode_' + x).classList.toggle('active', x === data.mode_id);
            }
            syncMobileNav(data.mode_id);

            $id('dutycycle').textContent = (data.evse.pwm * 100 / 1024).toFixed(0) + " %";
            if (data.mode_id == 2) { /* SOLAR MODE */
                showAll('.with_solar');
                hideById('override_current_box');
                hideById('override_current_box2');
            } else {
                hideAll('.with_solar');
                showById('override_current_box');
                showById('override_current_box2');
            }

            if (data.ev_state) {
                var full_soc = data.ev_state.full_soc;
                var initial_soc = data.ev_state.initial_soc;
                var computed_soc = data.ev_state.computed_soc;
                var time_until_full = data.ev_state.time_until_full;
                var energy_capacity = data.ev_state.energy_capacity;
                var evccid = data.ev_state.evccid;

                $id('computed_soc').innerHTML = computed_soc >= 0 ? computed_soc + " &#37;" : "N/A";
                $id('full_soc').innerHTML = full_soc >= 0 ? full_soc + " &#37;" : "N/A";
                $id('initial_soc').innerHTML = initial_soc >= 0 ? initial_soc + " &#37;" : "N/A";
                $id('energy_capacity').innerHTML = energy_capacity >= 0 ? (energy_capacity / 1000).toFixed(1) + " kWh" : "N/A";
                $id('evccid').innerHTML = evccid || "N/A";
                var fullAtEl = $id('full_at');
                fullAtEl.textContent = time_until_full > 0
                    ? new Date(+Date.now() + (time_until_full * 1000)).toLocaleString(undefined, { timeStyle: 'short', dateStyle: 'short' })
                    : 'N/A';
                fullAtEl.title = time_until_full > 0 ? Math.round(time_until_full / 60) + ' min to go' : 'N/A';
            }

            if (data.mqtt) {
                var mqttEl = $id('mqtt');
                var mqttOk = data.mqtt.status === 'Connected';
                mqttEl.textContent = data.mqtt.status || 'N/A';
                mqttEl.className = 'chip ' + (mqttOk ? 'chip-green' : 'chip-red');
                mqttEl.title = mqttOk ? 'MQTT broker connected' : 'MQTT broker disconnected';
                showEl(mqttEl);
                showById('mqtt_config');
            } else {
                var mqttEl2 = $id('mqtt');
                mqttEl2.textContent = '';
                hideEl(mqttEl2);
                hideAll('.config');
                hideById('mqtt_config');
            }

            var isSlave = data.evse.loadbl > 1;
            var isMaster = data.evse.loadbl == 1;
            if (isSlave) {
                showById('loadbl');
                showById('loadbl_text');
                $id('loadbl_node').textContent = "Slave Node " + (data.evse.loadbl - 1);
                hideById('contactor2');
                showById('mode_2');
                showById('mode_3');
                /* Show override current but grayed out on slave */
                showById('override_current_box');
                showById('override_current_box2');
                $qa('#form_pwm input, #form_pwm button, #form_pwm select').forEach(function(el) { el.disabled = true; el.style.opacity = '0.5'; el.title = SLAVE_TOOLTIP; });
            } else if (isMaster) {
                showById('loadbl');
                showById('loadbl_text');
                $id('loadbl_node').textContent = "Master";
                hideById('contactor2');
                showById('mode_2');
                showById('mode_3');
                $qa('#form_pwm input, #form_pwm button, #form_pwm select').forEach(function(el) { el.disabled = false; el.style.opacity = ''; el.title = ''; });
            } else {
                hideById('loadbl');
                hideById('loadbl_text');
                showById('contactor2');
                showById('mode_2');
                showById('mode_3');
                $qa('#form_pwm input, #form_pwm button, #form_pwm select').forEach(function(el) { el.disabled = false; el.style.opacity = ''; el.title = ''; });
            }
            /* Gray out slave-restricted settings. Also surface a visible
             * note next to the override dropdown so slave users understand
             * the restriction instead of silently wondering why clicking
             * NORMAL/SMART doesn't change the override. */
            setSlaveRestricted('mode_override_current', isSlave);
            if (isSlave) showById('override_slave_note');
            else         hideById('override_slave_note');
            setSlaveRestricted('solar_start_current', false); /* solar settings editable on all */
            setSlaveRestricted('solar_max_import_current', false);
            setSlaveRestricted('solar_stop_time', false);

            if (data.evse.loadbl == 1) {
                showAll('.with_scheduling');
                $id('prio_strategy').value = data.settings.prio_strategy;
                $id('rotation_interval').value = data.settings.rotation_interval;
                $id('idle_timeout').value = data.settings.idle_timeout;
                if (data.schedule) {
                    var states = data.schedule.state.join(', ');
                    $id('schedule_state').textContent = states;
                    $id('rotation_timer').textContent = data.schedule.rotation_timer + 's';
                }
                if (data.nodes) {
                    updateNodeOverview(data.nodes, data.settings.current_max);
                }
            } else {
                hideAll('.with_scheduling');
            }

            $id('car_connected').textContent = data.car_connected ? "Yes" : "No";
            $id('state').textContent = data.evse.state;
            last_evse_state_id = data.evse.last_state_id;
            updateStateDot(data.evse.state_id, data.evse.error_id);
            $id('temp').textContent = data.evse.temp + " \u00B0C / " + data.evse.temp_max + " \u00B0C";

            if (data.evse.error != "None") {
                $id('error').textContent = data.evse.error;
                showById('with_errors');
            } else {
                hideById('with_errors');
            }

            if (data.evse.rfid != "Not Installed") {
                $id('rfid').textContent = data.evse.rfid;
                showById('show_rfid');                /* fix: sibling to the hide branch — without this the RFID indicator stays hidden by HTML default */
            } else {
                hideById('show_rfid');
            }

            if (data.evse.solar_stop_timer > 0) {
                $id('state').textContent += " (Stopping in " + data.evse.solar_stop_timer + "s)";
            }

            $id('current_min').textContent = data.settings.current_min.toFixed(1) + " A";
            $id('current_max').textContent = data.settings.current_max.toFixed(1) + " A";
            $id('override_current').textContent = (data.settings.override_current / 10).toFixed(1) + " A";
            $id('enable_C2').textContent = data.settings.enable_C2;

            /* Capacity tariff */
            if (data.settings.capacity_limit !== undefined) {
                updateCapacityTariff(data.settings.capacity_limit,
                    data.settings.capacity_window_avg,
                    data.settings.capacity_monthly_peak,
                    data.settings.capacity_headroom);
            }

            // Format a Date as local YYYY-MM-DDTHH:MM for <input type="datetime-local">.
            // Must use LOCAL time — toISOString() returns UTC which the backend's
            // mktime() would misinterpret as local, shifting the schedule by the
            // timezone offset and potentially clearing it as "in the past".
            function toLocalDT(d) {
                var p = function(n) { return n < 10 ? '0' + n : '' + n; };
                return d.getFullYear() + '-' + p(d.getMonth() + 1) + '-' + p(d.getDate())
                    + 'T' + p(d.getHours()) + ':' + p(d.getMinutes());
            }
            if (data.settings.starttime) {
                var st = new Date(data.settings.starttime * 1000);
                $id('starttime_date_time').textContent = st.toLocaleDateString() + " " + st.toLocaleTimeString();
                var stInput = $id('starttime');
                if (stInput && !stInput.matches(':focus')) {
                    stInput.value = toLocalDT(st);
                }
            } else {
                $id('starttime_date_time').textContent = "none";
            }
            if (data.settings.stoptime) {
                var sp = new Date(data.settings.stoptime * 1000);
                $id('stoptime_date_time').textContent = sp.toLocaleDateString() + " " + sp.toLocaleTimeString();
                var spInput = $id('stoptime');
                if (spInput && !spInput.matches(':focus')) {
                    spInput.value = toLocalDT(sp);
                }
            } else {
                $id('stoptime_date_time').textContent = "none";
            }
            $id('repeat').textContent = data.settings.repeat == 1 ? "Daily" : "none";
            $id('daily_repeat').checked = data.settings.repeat == 1;

            $id('battery_current').textContent = (data.home_battery.current / 10).toFixed(1) + " A";

            $id('phase_total').textContent = (data.phase_currents.TOTAL / 10).toFixed(1) + " A";
            $id('phase_1').textContent = (data.phase_currents.L1 / 10).toFixed(1) + " A";
            $id('phase_2').textContent = (data.phase_currents.L2 / 10).toFixed(1) + " A";
            $id('phase_3').textContent = (data.phase_currents.L3 / 10).toFixed(1) + " A";
            maxMainsAmps = data.settings.current_main || 25;
            updatePhaseBars(data.phase_currents.L1, data.phase_currents.L2, data.phase_currents.L3);
            updatePowerFlow(data.phase_currents.TOTAL, data.ev_meter.import_active_power, data.home_battery.current);
            $id('evmeter_currents_total').textContent = (data.ev_meter.currents.TOTAL / 10).toFixed(1) + " A";
            $id('evmeter_currents_1').textContent = (data.ev_meter.currents.L1 / 10).toFixed(1) + " A";
            $id('evmeter_currents_2').textContent = (data.ev_meter.currents.L2 / 10).toFixed(1) + " A";
            $id('evmeter_currents_3').textContent = (data.ev_meter.currents.L3 / 10).toFixed(1) + " A";
            $id('charge_current').textContent = (data.settings.charge_current / 10).toFixed(1) + " A";

            $id('phase_original_total').textContent = (data.phase_currents.original_data.TOTAL / 10).toFixed(1) + " A";
            $id('phase_original_1').textContent = (data.phase_currents.original_data.L1 / 10).toFixed(1) + " A";
            $id('phase_original_2').textContent = (data.phase_currents.original_data.L2 / 10).toFixed(1) + " A";
            $id('phase_original_3').textContent = (data.phase_currents.original_data.L3 / 10).toFixed(1) + " A";

            if (data.phase_currents.last_data_update > 0) {
                $id('p1_data_time').textContent = new Date(data.phase_currents.last_data_update * 1000).toLocaleTimeString();
                $id('p1_data_date').textContent = new Date(data.phase_currents.last_data_update * 1000).toLocaleDateString();
                showById('with_p1_api_data_date');
                showById('with_p1_api_data_time');
            } else {
                hideById('with_p1_api_data_date');
                hideById('with_p1_api_data_time');
            }

            if (data.home_battery.last_update > 0) {
                $id('battery_last_update_time').textContent = new Date(data.home_battery.last_update * 1000).toLocaleTimeString();
                $id('battery_last_update_date').textContent = new Date(data.home_battery.last_update * 1000).toLocaleDateString();
                showById('with_homebattery');
            } else {
                hideById('with_homebattery');
            }

            if (data.home_battery.current == 0) {
                $id('battery_status').textContent = "Idle";
            } else {
                $id('battery_status').textContent = data.home_battery.current < 0 ? "Discharging" : "Charging";
            }

            if (data.settings.mains_meter === "Disabled") {
                hideAll('.with_mainsmeter');
            } else {
                showAll('.with_mainsmeter');
            }

            if (data.ev_meter.description == "Disabled") {
                $qa('[id=with_evmeter]').forEach(hideEl);
            } else {
                $qa('[id=with_evmeter]').forEach(showEl);
                $id('evmeter_description').textContent = data.ev_meter.description;
                var heroP = $id('evmeter_power');
                if (heroP) {
                    var kw = (data.ev_meter.import_active_power / 1000).toFixed(1);
                    heroP.innerHTML = kw + '<span class="power-unit">kW</span>';
                }
                $id('evmeter_total_kwh').textContent = (data.ev_meter.total_wh / 1000).toFixed(1) + " kWh";
                $id('evmeter_charged_kwh').textContent = (data.ev_meter.charged_wh / 1000).toFixed(1) + " kWh";
            }

            $id('solar_start_current').value = data.settings.solar_start_current;
            $id('solar_max_import_current').value = data.settings.solar_max_import;
            $id('solar_stop_time').value = data.settings.solar_stop_time;

            if (data.settings.modem == "Experiment" || data.settings.modem == "QCA7000") {
                showAll('.with_modem');
            } else {
                hideAll('.with_modem');
            }

            if (data.mqtt) {
                fetchMqttCert();
                $id('mqtt_host').value = data.mqtt.host;
                $id('mqtt_port').value = data.mqtt.port;
                $id('mqtt_username').value = data.mqtt.username;
                /* Security: backend never returns the MQTT password; it sends
                 * password_set: bool. Mirror the OCPP auth_key pattern — show
                 * bullets when a password is configured so the user can tell
                 * it is set without exposing the value. configureMqtt() skips
                 * the field if left as bullets or empty, preventing the
                 * save-without-retyping wipe (previously the field was set to
                 * `undefined`, turning a stray save into an accidental clear). */
                $id('mqtt_password').value = data.mqtt.password_set ? '••••••••' : '';
                $id('mqtt_password').placeholder = data.mqtt.password_set
                    ? 'Password configured (enter new value to replace)'
                    : 'Leave empty for anonymous MQTT';
                $id('mqtt_topic_prefix').value = data.mqtt.topic_prefix;
                $id('mqtt_tls').checked = data.mqtt.tls;
                if (data.mqtt.change_only !== undefined)
                    $id('mqtt_change_only').checked = data.mqtt.change_only;
                if (data.mqtt.heartbeat !== undefined)
                    $id('mqtt_heartbeat').value = data.mqtt.heartbeat;
                $id('mqtt_ca_cert').value = data.mqtt.ca_cert || '';
                toggleCertVisibility();
            }

            $id('lcdlock').checked = data.settings.lcdlock == 1;

            if (data.settings.lock != 0) {
                $id('cablelock').checked = data.settings.cablelock == 1;
            } else {
                hideById('cablelock');
                hideEl($id('cablelock_label'));
            }

            if (data.ocpp) {
                /* Reveal the outer card whenever the backend is shipping OCPP
                 * data (always true on builds compiled with ENABLE_OCPP). The
                 * matching hideById in the else branch keeps non-OCPP builds
                 * silent. Without this, the HTML default `display:none` left
                 * the panel permanently invisible (issue #129). */
                showById('ocpp_config_outer');
                if (data.ocpp.mode == "Enabled") {
                    showById('ocpp_settings');
                    $id('enable_ocpp').checked = true;
                } else {
                    hideById('ocpp_settings');
                    $id('enable_ocpp').checked = false;
                }

                if (data.ocpp.auto_auth == "Enabled") {
                    showById('ocpp_auto_auth_idtag_wrapper');
                    $id('ocpp_auto_auth').checked = true;
                } else {
                    hideById('ocpp_auto_auth_idtag_wrapper');
                    $id('ocpp_auto_auth').checked = false;
                }

                /* OCPP panel is always editable (no edit-mode toggle). Non-secret
                 * fields are only populated on first load (when still empty) so
                 * the 5s background poll never clobbers what the user is typing.
                 * The auth_key field is the secret-handling exception: always
                 * reflects the backend state (bullets if set, empty if not), and
                 * configureOcpp() skips submitting it when the value is still
                 * bullets or empty. See Security C-2. */
                var urlField  = $id('ocpp_backend_url');
                var cbIdField = $id('ocpp_cb_id');
                var idTagField = $id('ocpp_auto_auth_idtag');
                if (!urlField.value)  urlField.value  = data.ocpp.backend_url || '';
                if (!cbIdField.value) cbIdField.value = data.ocpp.cb_id || '';
                if (!idTagField.value) idTagField.value = data.ocpp.auto_auth_idtag || '';
                $id('ocpp_auth_key').value = data.ocpp.auth_key_set ? '••••••••' : '';
                $id('ocpp_auth_key').placeholder = data.ocpp.auth_key_set
                    ? 'Auth key configured (enter new value to replace)'
                    : 'Only for SP2 connections';

                $id('ocpp_ws_status').textContent = data.ocpp.status;
            } else {
                hideById('ocpp_config_outer');
            }

            /* Only continue polling if WebSocket is not connected */
            if (!wsConnected) setTimeout(loadData, 5000);
        });
}

/* ========== Settings functions ========== */
function SolStartCurr() {
    fetch("/settings?solar_start_current=" + $id('solar_start_current').value, { method: 'POST' , body: '' });
}
function SolImportCurr() {
    fetch("/settings?solar_max_import=" + $id('solar_max_import_current').value, { method: 'POST' , body: '' });
}
function SolStopTime() {
    fetch("/settings?stop_timer=" + $id('solar_stop_time').value, { method: 'POST' , body: '' });
}
function setPrioStrategy() {
    fetch("/settings?prio_strategy=" + $id('prio_strategy').value, { method: 'POST' , body: '' });
}
function setRotationInterval() {
    fetch("/settings?rotation_interval=" + $id('rotation_interval').value, { method: 'POST' , body: '' });
}
function setIdleTimeout() {
    fetch("/settings?idle_timeout=" + $id('idle_timeout').value, { method: 'POST' , body: '' });
}

/* ========== Mode activation ========== */
function activate(mode) {
    var params = new URLSearchParams({ mode: '' + mode });
    // Schedule params (starttime/stoptime/repeat) only make sense for
    // charging modes (1=Normal, 2=Solar, 3=Smart). OFF (0) and PAUSE (4)
    // just switch access state — sending schedule params would
    // unnecessarily trigger the backend's schedule parser and risk
    // overwriting or clearing a stored schedule.
    if ([1, 2, 3].includes(mode)) {
        var starttime = $qs('input[name="starttime"]').value;
        if (starttime) {
            params.append('starttime', starttime);
            var stoptime = $qs('input[name="stoptime"]').value;
            if (stoptime) params.append('stoptime', stoptime);
            params.append('repeat', '' + (+$qs('#daily_repeat').checked));
        }
    }
    if ([1, 2, 3].includes(mode)) {
        /* Only send override_current when the dropdown is NOT disabled.
         * On slave nodes (LoadBl >= 2) the dropdown is disabled via
         * setSlaveRestricted() — the backend would reject the value with
         * "Value not allowed!" and leave the response confusing. Skipping
         * the param keeps the mode-activation clean on slaves. */
        var overrideEl = $qs('#mode_override_current');
        if (overrideEl && !overrideEl.disabled) {
            params.append('override_current', '' + (overrideEl.value * 10));
        }
    }
    fetch(endpoint + '?' + params, { method: 'POST' , body: '' });

    /* Immediate visual feedback */
    $qs('#mode').textContent = $qs('#mode_' + mode).textContent;
    for (var x of [0, 1, 2, 3, 4]) {
        $qs('#mode_' + x).classList.toggle('active', x === mode);
    }
}

/* ========== MQTT config ========== */
/* MQTT settings are always visible — no toggle needed. Fetch cert on first load. */
function toggleMqttEdit() { /* kept for backward compat, no-op */ }
var mqttCertFetched = false;
function fetchMqttCert() {
    if (mqttCertFetched) return;
    mqttCertFetched = true;
    fetch("/mqtt_ca_cert").then(function(r) { return r.text(); }).then(function(certData) {
        $id('mqtt_ca_cert').value = certData;
    }).catch(function() {});
}

function configureMqtt() {
    var params = {
        mqtt_update:       1,
        mqtt_host:         $id('mqtt_host').value,
        mqtt_port:         $id('mqtt_port').value,
        mqtt_username:     $id('mqtt_username').value,
        mqtt_topic_prefix: $id('mqtt_topic_prefix').value,
        mqtt_tls:          $id('mqtt_tls').checked ? 1 : 0,
        mqtt_ca_cert:      $id('mqtt_ca_cert').value,
        mqtt_change_only:  $id('mqtt_change_only').checked ? 1 : 0,
        mqtt_heartbeat:    $id('mqtt_heartbeat').value
    };
    /* Only send mqtt_password when the user typed a real new value. Empty means
     * "anonymous MQTT or keep existing"; bullets means "keep the hidden stored
     * password". Sending either back would either wipe the secret or overwrite
     * it with literal bullets. Mirrors the configureOcpp() auth_key handling. */
    var pwd = $id('mqtt_password').value;
    if (pwd && pwd !== '••••••••') {
        params.mqtt_password = pwd;
    }
    var query = Object.keys(params)
        .map(function(k) { return k + "=" + encodeURIComponent(params[k]); })
        .join("&");
    fetch("/settings?" + query, { method: 'POST' , body: '' });
    alert('Settings applied');
    toggleMqttEdit();
}

/* ========== Control & Schedule save ========== */
/* Batches all Control & Schedule settings into one POST — same pattern
 * as Save MQTT / Save OCPP. Includes solar thresholds, override current,
 * schedule (starttime/stoptime/repeat), and lock toggles. */
function saveControlSchedule() {
    var params = {
        solar_start_current: $id('solar_start_current').value,
        solar_max_import:    $id('solar_max_import_current').value,
        stop_timer:          $id('solar_stop_time').value,
        lcdlock:             $id('lcdlock').checked ? 1 : 0
    };
    var cableLockEl = $id('cablelock');
    if (cableLockEl) params.cablelock = cableLockEl.checked ? 1 : 0;
    var overrideEl = $id('mode_override_current');
    if (overrideEl && !overrideEl.disabled) {
        params.override_current = '' + (overrideEl.value * 10);
    }
    // Schedule — starttime/stoptime/repeat can now be saved without a mode param
    var starttime = $qs('input[name="starttime"]').value;
    if (starttime) {
        params.starttime = starttime;
        var stoptime = $qs('input[name="stoptime"]').value;
        if (stoptime) params.stoptime = stoptime;
        params.repeat = '' + (+$qs('#daily_repeat').checked);
    }
    var query = Object.keys(params)
        .map(function(k) { return k + '=' + encodeURIComponent(params[k]); })
        .join('&');
    fetch('/settings?' + query, { method: 'POST', body: '' })
        .then(function() { loadData(); alert('Settings saved'); });
}

/* ========== Checkbox toggles ========== */
function toggleLCDlock() {
    fetch("/settings?lcdlock=" + ($id('lcdlock').checked ? 1 : 0), { method: 'POST' , body: '' });
}
function toggleCableLock() {
    fetch("/settings?cablelock=" + ($id('cablelock').checked ? 1 : 0), { method: 'POST' , body: '' });
}
function toggleEnableOcpp() {
    fetch("/settings?ocpp_update=1&ocpp_mode=" + ($id('enable_ocpp').checked ? 1 : 0), { method: 'POST' , body: '' })
        .then(loadData);   /* Refresh immediately so the inner OCPP fields appear/disappear without a 5s wait */
}
function toggleEnableOcppAutoAuth() {
    fetch("/settings?ocpp_update=1&ocpp_auto_auth=" + ($id('ocpp_auto_auth').checked ? 1 : 0), { method: 'POST' , body: '' });
    if ($id('ocpp_auto_auth').checked) {
        loadData();
    }
}

/* ========== OCPP config ========== */
/* OCPP settings panel matches MQTT UX: fields are always editable, a single
 * "Save OCPP" button invokes configureOcpp() directly (no Edit-mode toggle).
 * The legacy edit-mode toggle is kept as a no-op stub in case anything calls
 * it externally (same pattern as toggleMqttEdit). */
function toggleOcppEdit() { /* kept for backward compat, no-op */ }

function configureOcpp() {
    var params = {
        ocpp_update:          1,
        ocpp_backend_url:     $id('ocpp_backend_url').value,
        ocpp_cb_id:           $id('ocpp_cb_id').value,
        ocpp_auto_auth_idtag: $id('ocpp_auto_auth_idtag').value
    };
    /* Security C-2: only include ocpp_auth_key when the user actually typed a
     * new value. The displayed placeholder '••••••••' is a stand-in for the
     * configured-but-hidden key — sending it back would overwrite the real
     * secret with bullets. Skip the field if it's empty or still the placeholder. */
    var key = $id('ocpp_auth_key').value;
    if (key && key !== '••••••••') {
        params.ocpp_auth_key = key;
    }
    var query = Object.keys(params)
        .map(function(k) { return k + "=" + encodeURIComponent(params[k]); })
        .join("&");
    fetch("/settings?" + query, { method: 'POST' , body: '' });
}

/* ========== Actions ========== */
function reboot(event) {
    event && event.preventDefault();
    var httpStatus;
    fetch("/reboot")
        .then(function(response) {
            httpStatus = response.status;
            return response.text();
        })
        .then(function(message) {
            document.body.innerHTML = '<div id="rebootMsg" class="alert alert-success" role="alert"></div>';
            $qs('#rebootMsg').innerText = message;
            if (httpStatus === 200) {
                setInterval(function() { $qs('#rebootMsg').innerText += '.'; }, 500);
                setInterval(function() { window.location.reload(); }, 5000);
            } else {
                $qs('#rebootMsg').innerHTML
                    += '<br><br><a href="#" class="alert-link" onclick="window.location.reload()">Return to webinterface</a>';
            }
        })
        .catch(function(error) {
            document.body.innerHTML = '<div id="errorMsg" class="alert alert-danger" role="alert"></div>';
            $qs('#errorMsg').innerText = 'Error: ' + error;
        });
}

function postPWM(value) {
    fetch("/settings?override_pwm=" + value, { method: 'POST' , body: '' });
}

function postRequiredEVCCID() {
    fetch("/settings?required_evccid=" + $id('required_evccid').value, { method: 'POST' , body: '' });
}

/* ========== Diagnostic Telemetry Viewer ========== */
var diagWs = null;
var diagMaxRows = 100;
var diagActiveProfile = 'general';
var diagStateNames = ['A','B','C','D','COMM_B','COMM_B_OK','COMM_C','COMM_C_OK','Activate','B1','C1','MODEM_REQ','MODEM_WAIT','MODEM_DONE','MODEM_DEN'];
var diagModeNames = ['NRM','SMT','SOL'];
var diagAccessNames = ['OFF','ON','PAUSE'];
var diagPrevState = -1;

function diagParseSnapshot(buf) {
    var dv = new DataView(buf);
    return {
        ts: dv.getUint32(0, true), state: dv.getUint8(4), err: dv.getUint8(5),
        delay: dv.getUint8(6), access: dv.getUint8(7), mode: dv.getUint8(8),
        mL1: dv.getInt16(9, true), mL2: dv.getInt16(11, true), mL3: dv.getInt16(13, true),
        evL1: dv.getInt16(15, true), evL2: dv.getInt16(17, true), evL3: dv.getInt16(19, true),
        isum: dv.getInt16(21, true),
        chgA: dv.getUint16(23, true), isetBal: dv.getInt16(25, true), overrideA: dv.getUint16(27, true),
        solTmr: dv.getUint16(29, true), importA: dv.getUint16(31, true), startA: dv.getUint16(33, true),
        stateTmr: dv.getUint8(35), c1Tmr: dv.getUint8(36), accessTmr: dv.getUint8(37), noCurr: dv.getUint8(38),
        phases: dv.getUint8(39), swC2: dv.getUint8(40), enC2: dv.getUint8(41),
        loadBl: dv.getUint8(42), balSt0: dv.getUint8(43), bal0: dv.getUint16(44, true),
        temp: dv.getInt8(46), rcmon: dv.getUint8(47), pilot: dv.getUint8(48),
        mmTimeout: dv.getUint8(49), evmTimeout: dv.getUint8(50), mmType: dv.getUint8(51), evmType: dv.getUint8(52),
        rssi: dv.getInt8(53), mqtt: dv.getUint8(54)
    };
}

function diagFormatRow(s) {
    var sev = s.err > 0 ? ' sev-err' : (s.solTmr > 0 || s.delay > 0) ? ' sev-warn' : '';
    var stName = diagStateNames[s.state] || s.state;
    var stChanged = (diagPrevState !== -1 && s.state !== diagPrevState);
    diagPrevState = s.state;
    var m = Math.floor(s.ts / 60), sec = s.ts % 60;
    var ts = m + ':' + (sec < 10 ? '0' : '') + sec;
    var base = '<span class="diag-ts">' + ts + '</span>' +
        '<span class="diag-state">' + stName + (stChanged ? ' &larr;' : '') + '</span> ' +
        (diagAccessNames[s.access] || '?') + '/' + (diagModeNames[s.mode] || '?');
    var detail = '';
    switch (diagActiveProfile) {
    case 'solar':
        detail = ' | ' + (s.chgA / 10).toFixed(1) + 'A | Isum:' + (s.isum / 10).toFixed(1) +
            ' | sol:' + s.solTmr + 's imp:' + (s.importA / 10).toFixed(0) + ' start:' + (s.startA / 10).toFixed(0) +
            ' | ' + s.phases + 'P sw:' + s.swC2;
        break;
    case 'loadbal':
        detail = ' | ' + (s.chgA / 10).toFixed(1) + 'A Iset:' + (s.isetBal / 10).toFixed(1) +
            ' Bal[0]:' + (s.bal0 / 10).toFixed(1) + ' BS:' + s.balSt0 +
            ' | M:' + (s.mL1 / 10).toFixed(0) + '/' + (s.mL2 / 10).toFixed(0) + '/' + (s.mL3 / 10).toFixed(0) +
            ' | noCur:' + s.noCurr;
        break;
    case 'modbus':
        detail = ' | MM:' + s.mmType + ' t/o:' + s.mmTimeout + ' EVM:' + s.evmType + ' t/o:' + s.evmTimeout +
            ' | M:' + (s.mL1 / 10).toFixed(1) + '/' + (s.mL2 / 10).toFixed(1) + '/' + (s.mL3 / 10).toFixed(1) +
            ' | EV:' + (s.evL1 / 10).toFixed(1) + '/' + (s.evL2 / 10).toFixed(1) + '/' + (s.evL3 / 10).toFixed(1);
        break;
    case 'fast':
        detail = ' | ' + (s.chgA / 10).toFixed(1) + 'A | M:' + (s.mL1 / 10).toFixed(0) + '/' + (s.mL2 / 10).toFixed(0) + '/' + (s.mL3 / 10).toFixed(0) +
            ' Isum:' + (s.isum / 10).toFixed(1) + ' | EV:' + (s.evL1 / 10).toFixed(0) + '/' + (s.evL2 / 10).toFixed(0) + '/' + (s.evL3 / 10).toFixed(0) +
            ' | t:' + s.stateTmr + ' p:' + s.pilot;
        break;
    default: /* general */
        detail = ' | ' + (s.chgA / 10).toFixed(1) + 'A | M:' +
            (s.mL1 / 10).toFixed(0) + '/' + (s.mL2 / 10).toFixed(0) + '/' + (s.mL3 / 10).toFixed(0) +
            ' | ' + s.temp + '\u00B0C' + ' rssi:' + s.rssi;
    }
    if (s.err > 0) detail += ' | ERR:0x' + s.err.toString(16);
    return '<div class="diag-row' + sev + '">' + base + detail + '</div>';
}

function diagStart() {
    var profile = $id('diag_profile').value;
    diagActiveProfile = profile;
    diagPrevState = -1;
    $id('diag_log').innerHTML = '';
    /* Only update badge + connect WS if the backend actually accepted the start.
     * Previously .then() fired unconditionally — if the POST was rejected (e.g.
     * 411 Length Required on some browsers without explicit CL:0, now fixed
     * via body:'' — or 401 under AuthMode=1), the UI looked "Capturing" but
     * no data streamed. */
    fetch('/diag/start?profile=' + profile, { method: 'POST' , body: '' }).then(function(r) {
        if (!r.ok) {
            $id('diag_status_badge').className = 'diag-badge diag-badge-off';
            $id('diag_status_badge').textContent = 'Err ' + r.status;
            $id('diag_log').innerHTML = '<div class="diag-row sev-err">Start failed: HTTP ' + r.status + '</div>';
            return;
        }
        diagConnectWs();
        $id('diag_status_badge').className = 'diag-badge diag-badge-on';
        $id('diag_status_badge').textContent = 'Capturing';
    });
}

function diagStop() {
    fetch('/diag/stop', { method: 'POST' , body: '' }).then(function() {
        $id('diag_status_badge').className = 'diag-badge diag-badge-off';
        $id('diag_status_badge').textContent = 'Stopped';
    });
    if (diagWs) diagWs.close();
}

function diagConnectWs() {
    if (diagWs && diagWs.readyState === WebSocket.OPEN) return;
    var url = (window.location.protocol === 'https:' ? 'wss:' : 'ws:') + '//' + window.location.host + '/diag/stream';
    var ws = new WebSocket(url);
    diagWs = ws;
    ws.binaryType = 'arraybuffer';
    var rowCount = 0;
    ws.onmessage = function(ev) {
        if (!(ev.data instanceof ArrayBuffer) || ev.data.byteLength < 64) return;
        var snap = diagParseSnapshot(ev.data);
        var log = $id('diag_log');
        if (!log) return;
        log.insertAdjacentHTML('beforeend', diagFormatRow(snap));
        rowCount++;
        while (log.children.length > diagMaxRows) log.removeChild(log.firstChild);
        log.scrollTop = log.scrollHeight;
        var c = $id('diag_count');
        if (c) c.textContent = rowCount + ' samples';
    };
    ws.onclose = function() {
        diagWs = null;
        /* Only surface a close-reason when the user had an active capture
         * (badge is "Capturing") — don't spam on page-unload or manual stop. */
        var badge = $id('diag_status_badge');
        if (badge && badge.textContent === 'Capturing') {
            badge.className = 'diag-badge diag-badge-off';
            badge.textContent = 'WS closed';
        }
    };
    ws.onerror = function() {
        var log = $id('diag_log');
        if (log) log.insertAdjacentHTML('beforeend', '<div class="diag-row sev-err">WebSocket error</div>');
        if (ws.readyState !== WebSocket.CLOSED) ws.close();
    };
}

/* Check diag status on load */
fetch('/diag/status').then(function(r) { return r.json(); }).then(function(d) {
    if (d && d.profile && d.profile !== 'off') {
        $id('diag_status_badge').className = 'diag-badge diag-badge-on';
        $id('diag_status_badge').textContent = d.profile;
        diagConnectWs();
    }
}).catch(function() {});

/* ========== LCD WebSocket (IIFE) ========== */
(function() {
    var LCD_SCREEN = $qs('#lcd .lcd-screen');
    var LCD_BUTTON_CONTAINERS = $qa('#lcd .lcd-buttons, #lcd .lcd-display-buttons');
    var LCD_ACTIVATE = $qs('#lcd .lcd-activate');
    var PASSWORD_FIELD = $qs('#lcd-password');
    var PASSWORD_FORM = $qs('#lcd-password-form');
    var LOCK_STATUS = $qs('#lcd-lock-status');
    var LOCK_HINT = $qs('#lcd-lock-hint');
    var SESSION_STORAGE_PIN_KEY = 'lcdPinCode';
    var LOCKED_PRESS_ALERT_THRESHOLD = 3;
    var MIN_BUTTON_PRESS_MS = 300;
    var WS_RECONNECT_BASE_MS = 1000;
    var WS_RECONNECT_MAX_MS = 10000;
    var WS_RECONNECT_JITTER_MS = 400;
    var WS_INITIAL_RETRY_MS = 750;
    var WS_INITIAL_RETRY_JITTER_MS = 250;
    var WS_CONNECT_TIMEOUT_MS = 5000;
    var WS_URL = (window.location.protocol === 'https:' ? 'wss:' : 'ws:') + '//' + window.location.host + '/ws/lcd';
    var passwordVerified = false;
    var blockedButtonPressCount = 0;
    var lcdSocket = null;
    var reconnectTimer = null;
    var connectTimeoutTimer = null;
    var reconnectAttempts = 0;
    var hasConnectedOnce = false;
    var wsPausedByVisibility = false;
    var activeFrameUrl = null;
    var activePointers = new Map();

    function setActivateText(text) {
        if (LCD_ACTIVATE) LCD_ACTIVATE.textContent = text;
    }

    function clearConnectTimeout() {
        if (connectTimeoutTimer !== null) {
            window.clearTimeout(connectTimeoutTimer);
            connectTimeoutTimer = null;
        }
    }

    function clearReconnectTimer() {
        if (reconnectTimer !== null) {
            window.clearTimeout(reconnectTimer);
            reconnectTimer = null;
        }
    }

    function shouldKeepWsActive() {
        return !wsPausedByVisibility;
    }

    function requestImmediateReconnect() {
        if (!shouldKeepWsActive()) return;
        reconnectAttempts = 0;
        clearReconnectTimer();
        connectLCDWebSocket();
    }

    function disconnectLCDWebSocket() {
        clearConnectTimeout();
        clearReconnectTimer();
        if (lcdSocket && (lcdSocket.readyState === WebSocket.OPEN || lcdSocket.readyState === WebSocket.CONNECTING)) {
            lcdSocket.close();
        }
    }

    function scheduleReconnect() {
        if (!shouldKeepWsActive()) return;
        if (reconnectTimer !== null) return;
        var backoffMs = hasConnectedOnce
            ? Math.min(WS_RECONNECT_BASE_MS * (Math.pow(2, reconnectAttempts)), WS_RECONNECT_MAX_MS)
            : WS_INITIAL_RETRY_MS;
        var jitterMs = hasConnectedOnce
            ? Math.floor(Math.random() * WS_RECONNECT_JITTER_MS)
            : Math.floor(Math.random() * WS_INITIAL_RETRY_JITTER_MS);
        var reconnectDelayMs = backoffMs + jitterMs;
        reconnectAttempts += 1;
        setActivateText('Reconnecting in ' + Math.ceil(reconnectDelayMs / 1000) + 's...');
        reconnectTimer = window.setTimeout(function() {
            reconnectTimer = null;
            connectLCDWebSocket();
        }, reconnectDelayMs);
    }

    function activateLCD() {
        if (!LCD_ACTIVATE) return;
        LCD_ACTIVATE.remove();
        LCD_ACTIVATE = null;
    }

    function getStoredPin() {
        try { return sessionStorage.getItem(SESSION_STORAGE_PIN_KEY); }
        catch (e) { return null; }
    }

    function storePin(pin) {
        try { sessionStorage.setItem(SESSION_STORAGE_PIN_KEY, pin); }
        catch (e) { /* ignore */ }
    }

    function clearStoredPin() {
        try { sessionStorage.removeItem(SESSION_STORAGE_PIN_KEY); }
        catch (e) { /* ignore */ }
    }

    function updateLockUi(isUnlocked) {
        if (isUnlocked) {
            LOCK_STATUS.textContent = 'Unlocked';
            LOCK_STATUS.style.color = '#1cc88a';
            LOCK_HINT.textContent = 'Buttons are unlocked. You can use Left, Middle and Right.';
        } else {
            LOCK_STATUS.textContent = 'Locked - Enter PIN';
            LOCK_STATUS.style.color = '#e74a3b';
            LOCK_HINT.textContent = 'LCD buttons are locked. Enter your PIN to unlock them.';
        }
    }

    function connectLCDWebSocket() {
        if (!shouldKeepWsActive()) return;
        if (lcdSocket && (lcdSocket.readyState === WebSocket.OPEN || lcdSocket.readyState === WebSocket.CONNECTING)) return;

        setActivateText('Connecting...');
        var socket = new WebSocket(WS_URL);
        lcdSocket = socket;
        socket.binaryType = 'arraybuffer';

        clearConnectTimeout();
        connectTimeoutTimer = window.setTimeout(function() {
            if (socket.readyState === WebSocket.CONNECTING) socket.close();
        }, WS_CONNECT_TIMEOUT_MS);

        socket.onopen = function() {
            clearConnectTimeout();
            reconnectAttempts = 0;
            hasConnectedOnce = true;
            activateLCD();
        };

        socket.onmessage = function(event) {
            if (!(event.data instanceof ArrayBuffer)) return;
            var frameUrl = URL.createObjectURL(new Blob([event.data], { type: 'image/bmp' }));
            if (activeFrameUrl) URL.revokeObjectURL(activeFrameUrl);
            activeFrameUrl = frameUrl;
            LCD_SCREEN.src = frameUrl;
        };

        socket.onerror = function() {
            if (socket.readyState !== WebSocket.CLOSED) socket.close();
        };

        socket.onclose = function() {
            clearConnectTimeout();
            if (lcdSocket === socket) lcdSocket = null;
            if (shouldKeepWsActive()) scheduleReconnect();
        };
    }

    function sendButtonState(btnName, stateDown) {
        if (!passwordVerified) {
            blockedButtonPressCount += 1;
            updateLockUi(false);
            PASSWORD_FIELD.focus();
            PASSWORD_FIELD.select();
            if (blockedButtonPressCount >= LOCKED_PRESS_ALERT_THRESHOLD) {
                alert("Buttons are locked. Enter your PIN first to unlock.");
                blockedButtonPressCount = 0;
            }
            return;
        }
        if (!lcdSocket || lcdSocket.readyState !== WebSocket.OPEN) {
            requestImmediateReconnect();
            return;
        }
        lcdSocket.send(JSON.stringify({ button: btnName, state: stateDown ? 1 : 0 }));
    }

    function releasePointerButton(pointerId) {
        var pointerInfo = activePointers.get(pointerId);
        if (!pointerInfo || pointerInfo.released) return;
        pointerInfo.released = true;
        var elapsed = Date.now() - pointerInfo.pressTime;
        var sendRelease = function() {
            sendButtonState(pointerInfo.button, false);
            activePointers.delete(pointerId);
        };
        if (elapsed < MIN_BUTTON_PRESS_MS) {
            window.setTimeout(sendRelease, MIN_BUTTON_PRESS_MS - elapsed);
        } else {
            sendRelease();
        }
    }

    function verifyPassword(options) {
        options = options || {};
        var enteredPassword = options.password !== undefined ? options.password : PASSWORD_FIELD.value;
        var showErrorAlert = options.showErrorAlert !== undefined ? options.showErrorAlert : true;
        var rememberPin = options.rememberPin !== undefined ? options.rememberPin : true;
        fetch('/lcd-verify-password', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: new URLSearchParams({ password: enteredPassword })
        })
        .then(function(r) { return r.json().then(function(data) { return { status: r.status, data: data }; }); })
        .then(function(result) {
            var data = result.data;
            if (data && data.success) {
                passwordVerified = true;
                blockedButtonPressCount = 0;
                updateLockUi(true);
                if (rememberPin) storePin(enteredPassword);
                return;
            }
            passwordVerified = false;
            updateLockUi(false);
            clearStoredPin();
            if (!showErrorAlert) return;
            /* pin_not_configured: AuthMode is enabled but LCDPin is still 0.
             * The PIN can only be set from the physical LCD menu, so guide
             * the user there rather than the generic "Incorrect PIN". */
            if (data && data.error === 'pin_not_configured') {
                alert("No LCD PIN is configured on this device yet.\n\n" +
                      "Auth Mode is enabled but the PIN is still 0 (unset). " +
                      "Use the physical LCD menu on the charger to set a PIN, " +
                      "then return to this page to log in.");
                return;
            }
            if (result.status === 429 && data && data.rate_limited) {
                var wait = data.retry_after || 30;
                alert("Too many PIN attempts. Please wait " + wait + " seconds before trying again.");
                return;
            }
            alert("Incorrect PIN. Please try again.");
        })
        .catch(function() {
            passwordVerified = false;
            updateLockUi(false);
            if (showErrorAlert) alert("Could not reach the charger to verify the PIN.");
        });
    }

    PASSWORD_FORM.addEventListener('submit', function(event) {
        event.preventDefault();
        verifyPassword();
    });

    LCD_BUTTON_CONTAINERS.forEach(function(buttonContainer) {
        buttonContainer.addEventListener('pointerdown', function(event) {
            var button = event.target.closest('button[data-name]');
            if (!button) return;
            event.preventDefault();
            activePointers.set(event.pointerId, {
                button: button.dataset.name,
                pressTime: Date.now(),
                released: false
            });
            sendButtonState(button.dataset.name, true);
        });
    });

    ['pointerup', 'pointercancel'].forEach(function(eventName) {
        window.addEventListener(eventName, function(event) {
            releasePointerButton(event.pointerId);
        });
    });

    window.addEventListener('online', function() { requestImmediateReconnect(); });
    window.addEventListener('offline', function() { setActivateText('Offline - waiting for network...'); });

    document.addEventListener('visibilitychange', function() {
        if (document.hidden) {
            wsPausedByVisibility = true;
            setActivateText('Paused in background');
            disconnectLCDWebSocket();
            return;
        }
        wsPausedByVisibility = false;
        setActivateText('Reconnecting...');
        requestImmediateReconnect();
    });

    var storedPin = getStoredPin();
    if (storedPin) {
        PASSWORD_FIELD.value = storedPin;
        verifyPassword({ password: storedPin, showErrorAlert: false, rememberPin: false });
    } else {
        updateLockUi(false);
    }

    if (document.hidden) {
        wsPausedByVisibility = true;
        setActivateText('Paused in background');
    } else {
        requestImmediateReconnect();
    }

    /* Keyboard shortcuts for LCD buttons */
    document.addEventListener('keydown', function(e) {
        /* Only handle when not focused on an input field */
        var tag = document.activeElement && document.activeElement.tagName;
        if (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT') return;
        var btnName = null;
        if (e.key === 'ArrowLeft') btnName = 'left';
        else if (e.key === 'ArrowRight') btnName = 'right';
        else if (e.key === 'Enter') btnName = 'middle';
        if (btnName) {
            e.preventDefault();
            sendButtonState(btnName, true);
            setTimeout(function() { sendButtonState(btnName, false); }, 300);
        }
    });
})();

/* ========== Initialization ========== */
(function() {
    /* Set datetime inputs to current time */
    var now = new Date();
    var dateString = now.getFullYear() + '-' + ('0' + (now.getMonth() + 1)).slice(-2) + '-' + ('0' + now.getDate()).slice(-2);
    var timeString = ('0' + now.getHours()).slice(-2) + ':' + ('0' + now.getMinutes()).slice(-2);
    var dateTimeString = dateString + 'T' + timeString;

    $id('starttime').value = dateTimeString;
    $id('stoptime').value = dateTimeString;

    hideById('stoptime_group');
    hideById('daily_repeat_group');

    $id('starttime').addEventListener('change', function() { showById('stoptime_group'); });
    $id('stoptime').addEventListener('change', function() { showById('daily_repeat_group'); });
    $id('daily_repeat').checked = false;

    /* MQTT TLS checkbox listener */
    $id('mqtt_tls').addEventListener('change', toggleCertVisibility);

    /* Start data polling, then connect WebSocket for real-time updates */
    loadData();
    setTimeout(connectDataWs, 2000);

    /* ========== Hover tooltips ========== */
    /* Tooltips on LABELS and CONTAINERS (not value spans) so hovering the
     * description text shows the explanation. Applied to the closest
     * meaningful parent — .form-row, .detail-item, .info-row, or the
     * element itself for buttons. */
    var TIPS = {
        /* Hero — tooltips on elements not covered by HTML title= attributes */
        '#state':                'IEC 61851 charging state: A = no car, B = connected, C = charging, E = error',
        '#mode':                 'Active charging mode: Off, Normal, Solar, Smart, or Pause',
        '.power-big':            'Power being delivered to the car right now (W)',
        '.energy-row':           'Session energy (kWh) and current offered via the CP pilot (A)',
        /* Mode buttons */
        '#mode_0': 'Disable charging completely',
        '#mode_4': 'Pause active session without disconnecting the car',
        '#mode_1': 'Charge at a fixed current per phase (Override Current)',
        '#mode_2': 'Charge from solar surplus only (requires mains meter)',
        '#mode_3': 'Charge as fast as possible without overloading the mains (requires mains meter)',
        /* Mains, EV meter, current limits, capacity detail items —
         * tooltips applied directly in HTML via title= on .detail-item
         * and .phase-bar-row elements. Not duplicated here. */
        /* Home battery */
        '#battery_status':              'Home battery integration status',
        '#battery_current':             'Home battery charge/discharge current (A)',
        '#battery_last_update_date':    'Date of the last home-battery data update',
        '#battery_last_update_time':    'Time of the last home-battery data update',
        '#phase_original_total':        'Total mains current before home-battery compensation (A)',
        '#phase_original_1':            'L1 mains current before home-battery compensation (A)',
        '#phase_original_2':            'L2 before compensation (A)',
        '#phase_original_3':            'L3 before compensation (A)',
        /* EV state (modem / ISO 15118) */
        '#evccid':           'ISO 15118 EV Communication Controller ID',
        '#computed_soc':     'Current state of charge reported by the EV (%)',
        '#initial_soc':      'State of charge when the session started (%)',
        '#full_soc':         'Target state of charge for this session (%)',
        '#full_at':          'Estimated time until the target SoC is reached',
        '#energy_capacity':  'EV battery capacity as reported by the vehicle (kWh)',
        /* Solar settings */
        '#solar_start_current':      'Per-phase export (A) sustained before solar charging starts',
        '#solar_max_import_current': 'Grid top-up per phase (A) to reach the 6 A minimum',
        '#solar_stop_time':          'Minutes below threshold before solar charging stops',
        /* Override, schedule, locks — tooltips now in HTML title=
         * attributes on the .form-label spans. Not duplicated here. */
        /* Load balancing */
        '#prio_strategy':     'How current is distributed when multiple cars charge simultaneously',
        '#rotation_interval': 'Minutes between priority rotation among chargers (0 = off)',
        '#idle_timeout':      'Seconds before an idle (plugged-in, not charging) car loses its allocation',
        '#schedule_state':    'Current load-balancing scheduling state',
        '#rotation_timer':    'Seconds until the next priority rotation',
        /* Capacity tariff — tooltips now in HTML on .detail-item and
         * .form-label elements. Not duplicated here. */
        /* MQTT */
        '#mqtt_host':          'Broker hostname or IP. Leave empty to disable MQTT.',
        '#mqtt_port':          'Broker port: usually 1883 (plain) or 8883 (TLS)',
        '#mqtt_username':      'Broker username. Leave empty for anonymous.',
        '#mqtt_password':      'Broker password. Leave empty to keep existing.',
        '#mqtt_topic_prefix':  'MQTT topic root — default: SmartEVSE-<serial>',
        '#mqtt_tls':           'Enable TLS encryption for the broker connection',
        '#mqtt_change_only':   'Only publish when a value changes (reduces MQTT traffic 70-97%)',
        '#mqtt_heartbeat':     'Seconds between forced re-publish of unchanged values',
        '#mqtt_ca_cert':       'PEM-encoded CA certificate for TLS. Empty = Let\'s Encrypt default.',
        /* OCPP */
        '#enable_ocpp':            'Enable OCPP 1.6j backend connection',
        '#ocpp_ws_status':         'WebSocket connection status to the OCPP backend',
        '#ocpp_backend_url':       'WebSocket URL provided by your OCPP provider (ws:// or wss://)',
        '#ocpp_cb_id':             'Charge Box ID — identifies this charger at the backend',
        '#ocpp_auth_key':          'SP2 Basic-Auth password. Leave empty to keep existing.',
        '#ocpp_auto_auth':         'Auto-authorize every plug-in without RFID (FreeVend mode)',
        '#ocpp_auto_auth_idtag':   'Default idTag sent for auto-authorized sessions',
        /* Diagnostics */
        '#diag_profile': 'Capture profile: general, solar, loadbal, modbus, or fast'
    };
    /* Apply tooltips. For detail-item children, walk up to the parent row
     * so the tooltip fires on the label text, not only the value span. */
    Object.keys(TIPS).forEach(function(sel) {
        var el = $qs(sel);
        if (!el) return;
        /* If the element is inside a .detail-item, apply to the item row;
         * otherwise apply directly. Never overwrite an existing title. */
        var target = el.closest ? (el.closest('.detail-item') || el) : el;
        if (!target.title) target.title = TIPS[sel];
    });

    /* ========== Contextual help links ========== */
    var DOCS = 'https://github.com/basmeerman/SmartEVSE-3.5/blob/master/docs/';

    function mkHelp(page) {
        var a = document.createElement('a');
        a.href = DOCS + page;
        a.target = '_blank';
        a.rel = 'noopener';
        a.className = 'help-link';
        a.title = 'Documentation';
        a.textContent = '?';
        return a;
    }

    /* Field-level help links. For controls inside a .form-row, the link is
     * appended as the LAST child of the row (right-aligned), not inline
     * between the input and adjacent elements. */
    var HELP = {
        '#solar_start_current':      'solar-smart-stability.md#current-regulation',
        '#mode_override_current':    'guide-owner.md#3-starting-a-charge-session',
        '#starttime':                'guide-owner.md#scheduled--delayed',
        '#lock_row':                 'guide-owner.md#8-access-control',
        '#capacity_limit_input':     'guide-owner.md#7-common-adjustments',
        '#mqtt_host':                'guide-integrator.md#setup',
        '#mqtt_password':            'security.md#4-secret-redaction-in-get-settings',
        '#mqtt_tls':                 'guide-integrator.md#tls',
        '#mqtt_change_only':         'guide-integrator.md#change-only-publishing',
        '#ocpp_backend_url':         'guide-integrator.md#setup-1',
        '#ocpp_auth_key':            'security.md#4-secret-redaction-in-get-settings',
        '#enable_ocpp':              'guide-integrator.md#6-ocpp-backends',
        '#prio_strategy':            'priority-scheduling.md',
        '#rotation_interval':        'priority-scheduling.md',
        '#idle_timeout':             'priority-scheduling.md',
        '#diag_profile':             'troubleshooting.md#11-capturing-a-debug-log'
    };
    Object.keys(HELP).forEach(function(sel) {
        var el = $qs(sel);
        if (!el) return;
        var a = mkHelp(HELP[sel]);
        /* Append to the end of the .form-row (right side), or after the
         * element's parent label if not in a form-row. */
        var row = el.closest ? el.closest('.form-row') : null;
        if (row) {
            row.appendChild(a);
        } else {
            var parent = el.parentNode;
            if (parent) parent.appendChild(a);
        }
    });

    /* Section-toggle headers — match by card-title text */
    var SECTION_HELP = {
        'Mains Phases':         'guide-installer.md#7-meter-selection',
        'Current Limits':       'configuration.md#min',
        'Diagnostics':          'troubleshooting.md#11-capturing-a-debug-log',
        'Load Balancing':       'guide-owner.md#9-multi-charger-setup-masterslave',
        'Capacity Tariff':      'guide-owner.md#7-common-adjustments',
        'Control & Schedule':   'guide-owner.md#3-starting-a-charge-session',
        'MQTT Configuration':   'guide-integrator.md#3-home-assistant-via-mqtt',
        'OCPP Configuration':   'guide-integrator.md#6-ocpp-backends'
    };
    $qa('.section-toggle .card-title').forEach(function(title) {
        var text = title.textContent.trim();
        if (SECTION_HELP[text]) {
            var a = mkHelp(SECTION_HELP[text]);
            a.onclick = function(e) { e.stopPropagation(); };
            title.appendChild(a);
        }
    });
    /* EV Meter card-title is dynamic (set by JS from meter description) —
     * inject the help link on the static wrapper instead. */
    var evmTitle = $qs('#evmeter_description');
    if (evmTitle) {
        var ct = evmTitle.closest ? evmTitle.closest('.card-title') : null;
        if (ct) {
            var a = mkHelp('guide-installer.md#7-meter-selection');
            a.onclick = function(e) { e.stopPropagation(); };
            ct.appendChild(a);
        }
    }
    /* Hero status — help link at end of status-value row */
    var heroStatus = $qs('.hero .status-value');
    if (heroStatus) heroStatus.appendChild(mkHelp('guide-owner.md#4-reading-the-dashboard'));
    /* Mode bar — help link at end */
    var modeBar = $id('form_mode');
    if (modeBar) modeBar.appendChild(mkHelp('guide-owner.md#2-the-five-modes'));
})();
