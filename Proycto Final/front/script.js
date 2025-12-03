// Mensaje de inicio - DEBE aparecer en consola
console.log("=== SCRIPT.JS CARGADO ===");
console.log("Estado del DOM:", document.readyState);

// --- FORZAR ORIENTACIÓN VERTICAL EN MÓVIL ---
function lockOrientation() {
    // Intentar bloquear la orientación a vertical
    if (screen.orientation && screen.orientation.lock) {
        screen.orientation.lock('portrait').catch(function(err) {
            console.log('No se pudo bloquear la orientación:', err);
        });
    } else if (screen.lockOrientation) {
        screen.lockOrientation('portrait');
    } else if (screen.mozLockOrientation) {
        screen.mozLockOrientation('portrait');
    } else if (screen.msLockOrientation) {
        screen.msLockOrientation('portrait');
    }
}

// Detectar si es móvil
function isMobile() {
    return /Android|webOS|iPhone|iPad|iPod|BlackBerry|IEMobile|Opera Mini/i.test(navigator.userAgent) || 
           (window.innerWidth <= 768);
}

// Aplicar bloqueo de orientación si es móvil
if (isMobile()) {
    // Intentar bloquear al cargar
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', lockOrientation);
    } else {
        lockOrientation();
    }
    
    // Reintentar si cambia la orientación
    window.addEventListener('orientationchange', function() {
        setTimeout(lockOrientation, 100);
    });
    
    // También escuchar cambios de resize
    let resizeTimer;
    window.addEventListener('resize', function() {
        clearTimeout(resizeTimer);
        resizeTimer = setTimeout(lockOrientation, 100);
    });
}

// --- LÓGICA DE LOGIN ---
const loginForm = document.getElementById("loginForm");

if (loginForm) {
    const errorDiv = document.getElementById("error");

    loginForm.addEventListener("submit", async function(e) {
        e.preventDefault();
        errorDiv.textContent = "Verificando...";
        
        const formData = new FormData(loginForm);
        const user = formData.get("user");
        const pass = formData.get("pass");

        try {
            // Enviamos los datos como x-www-form-urlencoded (igual que tu original)
            const res = await fetch("/login", {
                method: "POST",
                headers: { "Content-Type": "application/x-www-form-urlencoded" },
                body: "user=" + encodeURIComponent(user) + "&pass=" + encodeURIComponent(pass)
            });

            const text = await res.text();

            if (res.ok) {
                // Si el ESP32 responde 200 OK, redirigimos al dashboard
                window.location.href = "/dashboard";
            } else {
                // Si responde 401, mostramos el error
                errorDiv.textContent = text || "Credenciales incorrectas.";
            }
        } catch (err) {
            console.error(err);
            errorDiv.textContent = "Error de conexión con el ESP32.";
        }
    });
}

// --- RELOJ EN TIEMPO REAL ---
function updateClock() {
    const clockElement = document.getElementById('clock');
    if (clockElement) {
        const now = new Date();
        const timeString = now.toLocaleTimeString('es-ES', { hour12: false });
        clockElement.innerText = timeString;
    }
}
setInterval(updateClock, 1000);
updateClock(); // Primera llamada inmediata
console.log("[CLOCK] Reloj inicializado");

// --- TEMPERATURA EN TIEMPO REAL ---
let isUpdatingTemperature = false; // Evitar peticiones superpuestas

function updateTemperature() {
    const tempElement = document.getElementById('tempDisplay');
    if (!tempElement) {
        return;
    }
    
    // Evitar peticiones superpuestas
    if (isUpdatingTemperature) {
        return;
    }
    
    isUpdatingTemperature = true;
    
    fetch("/temperature")
        .then(resp => {
            if (!resp.ok) {
                if (resp.status === 401) {
                    return Promise.reject(new Error("Unauthorized"));
                }
                throw new Error(`HTTP ${resp.status}`);
            }
            return resp.text().then(text => {
                try {
                    return JSON.parse(text);
                } catch (e) {
                    console.error("[TEMP] Error parseando JSON:", text);
                    throw e;
                }
            });
        })
        .then(data => {
            if (data && typeof data.temperature === 'number' && isFinite(data.temperature)) {
                const temp = data.temperature;
                if (temp > -900 && temp < 200) {
                    tempElement.innerText = `Temp: ${temp.toFixed(1)}°C`;
                } else {
                    tempElement.innerText = "Temp: --°C";
                }
            } else {
                tempElement.innerText = "Temp: --°C";
            }
        })
        .catch(err => {
            // No hacer nada si es error de autenticación (el usuario no está logueado)
            if (err.message !== "Unauthorized") {
                const tempEl = document.getElementById('tempDisplay');
                if (tempEl) {
                    tempEl.innerText = "Temp: --°C";
                }
            }
        })
        .finally(() => {
            isUpdatingTemperature = false;
        });
}
setInterval(updateTemperature, 1000);
updateTemperature(); // Primera llamada inmediata
console.log("[TEMP] Temperatura inicializada - actualización cada 1 segundo");

// --- LÓGICA DE LA TERMINAL ---
const term = document.getElementById("term");
const input = document.getElementById("inputLine");

if (term && input) {
    let lastActivity = Date.now();
    const SESSION_LIMIT = 3 * 60 * 1000; // 3 minutos

    // Historial y autocompletado
    const history = [];
    let historyIndex = -1;
    const commands = [
        "led y on", "led y off", "led b on", "led b off",
        "led all on", "led all off", "status", "help", "clear"
    ];

    function showWelcome() {
        term.innerHTML = "";
        const welcomeMsg = "Sistema iniciado correctamente.<br>Escribe 'help' para ver la lista de comandos.<br>";
        const separator = "<div class='separator-line'></div>";
        term.innerHTML = welcomeMsg + separator;
    }

    function appendLine(text) {
        term.innerHTML += text.replace(/</g, "&lt;").replace(/>/g, "&gt;") + "<br>";
        term.scrollTop = term.scrollHeight;
    }

    // Autocompletar con TAB
    function tryAutocomplete(current) {
        if (!current) return null;
        const matches = commands.filter(c => c.startsWith(current));
        if (matches.length === 0) return null;
        return matches[0];
    }

    function sendCommand(cmd) {
        appendLine("> " + cmd);
        fetch("/cmd?c=" + encodeURIComponent(cmd))
            .then(resp => {
                if (!resp.ok) { 
                    throw new Error("Sesión expirada. Redirigiendo..."); 
                }
                return resp.text();
            })
            .then(text => {
                appendLine(text);
                lastActivity = Date.now();
            })
            .catch(e => {
                appendLine("[ERROR] " + e.message);
                setTimeout(() => { doLogout(); }, 1200);
            });
    }

    input.addEventListener("keydown", function(e) {
        if (e.key === "Enter") {
            const raw = input.value.trim();
            if (raw.length === 0) return;
            const cmd = raw.toLowerCase();

            if (cmd === "clear") {
                showWelcome();
            } else {
                sendCommand(cmd);
                history.unshift(cmd);
                historyIndex = -1;
            }
            input.value = "";
            lastActivity = Date.now();
            e.preventDefault();
        } else if (e.key === "ArrowUp") {
            if (history.length === 0) return;
            if (historyIndex + 1 < history.length) historyIndex++;
            input.value = history[historyIndex];
            e.preventDefault();
        } else if (e.key === "ArrowDown") {
            if (historyIndex > 0) {
                historyIndex--;
                input.value = history[historyIndex];
            } else {
                historyIndex = -1;
                input.value = "";
            }
            e.preventDefault();
        } else if (e.key === "Tab") {
            e.preventDefault();
            const current = input.value.trim().toLowerCase();
            const auto = tryAutocomplete(current);
            if (auto) { 
                input.value = auto + " "; 
            }
        }
    });

    // Auto logout por inactividad
    setInterval(() => {
        if (Date.now() - lastActivity > SESSION_LIMIT) {
            appendLine("[INFO] Sesión expirada. Cerrando...");
            setTimeout(() => { doLogout(); }, 800);
        }
    }, 2000);

    showWelcome();
}

// Función para ir al dashboard
function goToDashboard() {
    window.location.href = "/dashboard";
}

// Función para suspender (bloquear) la sesión
function suspendSession() {
    doLogout();
}

// --- FUNCIONALIDAD PARA LA PÁGINA SLIDER ---

// Actualizar temperatura en el slider
function updateSliderTemperature() {
    const tempElement = document.getElementById('sliderTemperature');
    if (!tempElement) {
        return;
    }
    
    fetch("/temperature")
        .then(resp => {
            if (!resp.ok) {
                if (resp.status === 401) {
                    return Promise.reject(new Error("Unauthorized"));
                }
                throw new Error(`HTTP ${resp.status}`);
            }
            return resp.text().then(text => {
                try {
                    return JSON.parse(text);
                } catch (e) {
                    console.error("[SLIDER TEMP] Error parseando JSON:", text);
                    throw e;
                }
            });
        })
        .then(data => {
            if (data && typeof data.temperature === 'number' && isFinite(data.temperature)) {
                const temp = data.temperature;
                if (temp > -900 && temp < 200) {
                    tempElement.textContent = `${temp.toFixed(1)}°C`;
                } else {
                    tempElement.textContent = "--°C";
                }
            } else {
                tempElement.textContent = "--°C";
            }
        })
        .catch(err => {
            if (err.message !== "Unauthorized") {
                const tempEl = document.getElementById('sliderTemperature');
                if (tempEl) {
                    tempEl.textContent = "--°C";
                }
            }
        });
}

// Actualizar hora en el slider
function updateSliderClock() {
    const clockElement = document.getElementById('sliderClock');
    if (clockElement) {
        const now = new Date();
        const timeString = now.toLocaleTimeString('es-ES', { hour12: false });
        clockElement.textContent = timeString;
    }
}

// Variables globales para el modo manual del ventilador
let fanManualPowerState = false;
let fanManualSpeed = 0;

// Variables globales para el modo horario
let fanScheduleSpeed = 0;
let fanScheduleDay = 'lunes';
let fanScheduleTime = '00:00';
let fanScheduleRecords = []; // Array para almacenar los registros
let selectedScheduleRecord = null; // Registro seleccionado en el popup

// Variables globales para el modo temperatura
let fanTemperatureMode = false; // Si el modo temperatura está activo
let fanCurrentSpeed = 0; // Velocidad actual del ventilador
let fanTemperatureInterval = null; // Intervalo de monitoreo de temperatura

// Función para establecer el modo del ventilador
function setFanMode(mode) {
    // No hacer nada si estamos en modo manual o horario
    const manualView = document.getElementById('fanManualView');
    const scheduleView = document.getElementById('fanScheduleView');
    if ((manualView && manualView.style.display !== 'none') || 
        (scheduleView && scheduleView.style.display !== 'none')) {
        return;
    }
    
    // Remover clase active de todos los botones
    const buttons = document.querySelectorAll('.fan-btn');
    buttons.forEach(btn => btn.classList.remove('active'));
    
    // Agregar clase active al botón seleccionado
    const selectedBtn = document.querySelector(`.fan-btn[data-mode="${mode}"]`);
    if (selectedBtn) {
        selectedBtn.classList.add('active');
    }
    
    // Actualizar el texto de estado
    const statusText = document.getElementById('fanStatusText');
    if (statusText) {
        const modeNames = {
            'off': 'Apagado',
            'schedule': 'Horario',
            'temperature': 'Temperatura'
        };
        statusText.textContent = modeNames[mode] || 'Desconocido';
    }
    
    // Manejar el modo temperatura
    if (mode === 'temperature') {
        fanTemperatureMode = true;
        startTemperatureMode();
    } else {
        fanTemperatureMode = false;
        stopTemperatureMode();
    }
    
    // Si el modo es 'off', apagar el ventilador
    if (mode === 'off') {
        fanCurrentSpeed = 0;
        stopTemperatureMode();
        // Aquí puedes agregar lógica para enviar comando de apagado al ESP32
        console.log('[FAN] Ventilador apagado');
    }
    
    console.log(`[FAN] Modo del ventilador cambiado a: ${mode}`);
}

// Función para detectar si es móvil
function isMobileDevice() {
    return /Android|webOS|iPhone|iPad|iPod|BlackBerry|IEMobile|Opera Mini/i.test(navigator.userAgent) || 
           (window.innerWidth <= 768);
}

// Función para entrar al modo manual
function enterFanManualMode() {
    const normalView = document.getElementById('fanNormalView');
    const manualView = document.getElementById('fanManualView');
    
    if (normalView && manualView) {
        const isMobile = isMobileDevice();
        const transitionTime = isMobile ? 250 : 300; // Más rápido en móvil
        
        normalView.style.display = 'none';
        manualView.style.display = 'block';
        
        // Animación de entrada
        manualView.style.opacity = '0';
        manualView.style.transform = 'translateY(10px)';
        setTimeout(() => {
            manualView.style.transition = `opacity ${transitionTime}ms ease, transform ${transitionTime}ms ease`;
            manualView.style.opacity = '1';
            manualView.style.transform = 'translateY(0)';
        }, 10);
        
        console.log('[FAN] Modo manual activado');
    }
}

// Función para salir del modo manual
function exitFanManualMode() {
    const normalView = document.getElementById('fanNormalView');
    const manualView = document.getElementById('fanManualView');
    
    if (normalView && manualView) {
        const isMobile = isMobileDevice();
        const transitionTime = isMobile ? 250 : 300; // Más rápido en móvil
        
        // Animación de salida
        manualView.style.transition = `opacity ${transitionTime}ms ease, transform ${transitionTime}ms ease`;
        manualView.style.opacity = '0';
        manualView.style.transform = 'translateY(10px)';
        
        setTimeout(() => {
            manualView.style.display = 'none';
            normalView.style.display = 'block';
            
            // Resetear animación para la próxima vez
            manualView.style.opacity = '1';
            manualView.style.transform = 'translateY(0)';
        }, transitionTime);
        
        console.log('[FAN] Modo manual desactivado');
    }
}

// Función para alternar el estado de encendido/apagado del ventilador
function toggleFanPower() {
    fanManualPowerState = !fanManualPowerState;
    
    const powerIcon = document.getElementById('fanPowerIcon');
    const powerText = document.getElementById('fanPowerText');
    const powerToggle = document.getElementById('fanPowerToggle');
    const slider = document.getElementById('fanSlider');
    
    if (fanManualPowerState) {
        powerIcon.textContent = '▶';
        powerText.textContent = 'Encendido';
        powerToggle.classList.add('fan-power-on');
        slider.disabled = false;
        console.log('[FAN] Ventilador encendido');
    } else {
        powerIcon.textContent = '⏸';
        powerText.textContent = 'Apagado';
        powerToggle.classList.remove('fan-power-on');
        slider.disabled = true;
        // Resetear velocidad a 0 cuando se apaga
        slider.value = 0;
        updateFanSlider(0);
        console.log('[FAN] Ventilador apagado');
    }
    
    // Aquí puedes agregar lógica para enviar el comando al ESP32
    // fetch(`/fan/manual?power=${fanManualPowerState ? 'on' : 'off'}&speed=${fanManualSpeed}`)
    //     .then(resp => resp.text())
    //     .then(data => console.log(data))
    //     .catch(err => console.error(err));
}

// Función para actualizar el slider de velocidad
function updateFanSlider(value) {
    fanManualSpeed = parseInt(value);
    
    const sliderValue = document.getElementById('fanSliderValue');
    const sliderFill = document.getElementById('fanSliderFill');
    
    if (sliderValue) {
        sliderValue.textContent = fanManualSpeed + '%';
    }
    
    if (sliderFill) {
        sliderFill.style.width = fanManualSpeed + '%';
    }
    
    // Si el ventilador está encendido, enviar el comando
    if (fanManualPowerState) {
        console.log(`[FAN] Velocidad ajustada a: ${fanManualSpeed}%`);
        
        // Aquí puedes agregar lógica para enviar el comando al ESP32
        // fetch(`/fan/manual?power=on&speed=${fanManualSpeed}`)
        //     .then(resp => resp.text())
        //     .then(data => console.log(data))
        //     .catch(err => console.error(err));
    }
}

// ========== FUNCIONES DEL MODO HORARIO ==========

// Función para entrar al modo horario
function enterFanScheduleMode() {
    const normalView = document.getElementById('fanNormalView');
    const scheduleView = document.getElementById('fanScheduleView');
    
    if (normalView && scheduleView) {
        const isMobile = isMobileDevice();
        const transitionTime = isMobile ? 250 : 300;
        
        // Activar el botón de horario
        const scheduleBtn = document.querySelector('.fan-btn-schedule');
        if (scheduleBtn) {
            scheduleBtn.classList.add('active');
        }
        
        normalView.style.display = 'none';
        scheduleView.style.display = 'block';
        
        // Animación de entrada
        scheduleView.style.opacity = '0';
        scheduleView.style.transform = 'translateY(10px)';
        setTimeout(() => {
            scheduleView.style.transition = `opacity ${transitionTime}ms ease, transform ${transitionTime}ms ease`;
            scheduleView.style.opacity = '1';
            scheduleView.style.transform = 'translateY(0)';
        }, 10);
        
        console.log('[FAN] Modo horario activado');
    }
}

// Función para salir del modo horario
function exitFanScheduleMode() {
    const normalView = document.getElementById('fanNormalView');
    const scheduleView = document.getElementById('fanScheduleView');
    
    if (normalView && scheduleView) {
        const isMobile = isMobileDevice();
        const transitionTime = isMobile ? 250 : 300;
        
        // Animación de salida
        scheduleView.style.transition = `opacity ${transitionTime}ms ease, transform ${transitionTime}ms ease`;
        scheduleView.style.opacity = '0';
        scheduleView.style.transform = 'translateY(10px)';
        
        setTimeout(() => {
            scheduleView.style.display = 'none';
            normalView.style.display = 'block';
            
            // Resetear animación para la próxima vez
            scheduleView.style.opacity = '1';
            scheduleView.style.transform = 'translateY(0)';
        }, transitionTime);
        
        console.log('[FAN] Modo horario desactivado');
    }
}

// Función para actualizar el slider de velocidad del modo horario
function updateFanScheduleSlider(value) {
    fanScheduleSpeed = parseInt(value);
    
    const sliderValue = document.getElementById('fanScheduleSliderValue');
    const sliderFill = document.getElementById('fanScheduleSliderFill');
    
    if (sliderValue) {
        sliderValue.textContent = fanScheduleSpeed + '%';
    }
    
    if (sliderFill) {
        sliderFill.style.width = fanScheduleSpeed + '%';
    }
    
    console.log(`[FAN SCHEDULE] Velocidad ajustada a: ${fanScheduleSpeed}%`);
}

// Función para borrar la configuración actual del modo horario
function clearFanSchedule() {
    // Resetear valores
    fanScheduleSpeed = 0;
    fanScheduleDay = 'lunes';
    fanScheduleTime = '00:00';
    
    // Resetear UI
    const slider = document.getElementById('fanScheduleSlider');
    const daySelect = document.getElementById('fanScheduleDay');
    const timeInput = document.getElementById('fanScheduleTime');
    
    if (slider) {
        slider.value = 0;
        updateFanScheduleSlider(0);
    }
    
    if (daySelect) {
        daySelect.value = 'lunes';
    }
    
    if (timeInput) {
        timeInput.value = '00:00';
    }
    
    console.log('[FAN SCHEDULE] Configuración borrada');
    
    // Mostrar confirmación visual
    const deleteBtn = event.target.closest('.fan-schedule-btn-delete');
    if (deleteBtn) {
        deleteBtn.style.transform = 'scale(0.95)';
        setTimeout(() => {
            deleteBtn.style.transform = 'scale(1)';
        }, 150);
    }
}

// Función para registrar el horario programado
function registerFanSchedule() {
    const daySelect = document.getElementById('fanScheduleDay');
    const timeInput = document.getElementById('fanScheduleTime');
    
    if (!daySelect || !timeInput) {
        return;
    }
    
    fanScheduleDay = daySelect.value;
    fanScheduleTime = timeInput.value;
    
    // Crear el registro
    const record = {
        id: Date.now(), // ID único basado en timestamp
        day: fanScheduleDay,
        time: fanScheduleTime,
        speed: fanScheduleSpeed,
        date: new Date().toLocaleDateString('es-ES'),
        timestamp: new Date().toLocaleString('es-ES')
    };
    
    // Agregar al array de registros
    fanScheduleRecords.push(record);
    
    // Actualizar la lista de registros
    updateLogsDisplay();
    
    // Actualizar popup si está abierto
    const popup = document.getElementById('logsPopup');
    if (popup && popup.style.display !== 'none') {
        updateLogsPopupContent();
    }
    
    console.log('[FAN SCHEDULE] Registro creado:', record);
    
    // Mostrar mensaje de confirmación
    showScheduleConfirmationMessage();
    
    // Reiniciar campos (como si se diera a borrar)
    clearFanSchedule();
    
    // Aquí puedes agregar lógica para enviar el registro al ESP32
    // fetch('/fan/schedule', {
    //     method: 'POST',
    //     headers: { 'Content-Type': 'application/json' },
    //     body: JSON.stringify(record)
    // })
    // .then(resp => resp.text())
    // .then(data => console.log(data))
    // .catch(err => console.error(err));
}

// Función para mostrar mensaje de confirmación
function showScheduleConfirmationMessage(message) {
    // Crear o obtener el contenedor del mensaje
    let messageContainer = document.getElementById('scheduleConfirmationMessage');
    
    if (!messageContainer) {
        messageContainer = document.createElement('div');
        messageContainer.id = 'scheduleConfirmationMessage';
        messageContainer.className = 'schedule-confirmation-message';
        document.body.appendChild(messageContainer);
    }
    
    // Configurar el mensaje (usar mensaje personalizado o el por defecto)
    messageContainer.textContent = message || '✓ Registro guardado correctamente';
    messageContainer.style.display = 'block';
    messageContainer.style.opacity = '0';
    messageContainer.style.transform = 'translateY(-20px)';
    
    // Animación de entrada
    setTimeout(() => {
        messageContainer.style.transition = 'opacity 0.3s ease, transform 0.3s ease';
        messageContainer.style.opacity = '1';
        messageContainer.style.transform = 'translateY(0)';
    }, 10);
    
    // Ocultar después de 4 segundos
    setTimeout(() => {
        messageContainer.style.transition = 'opacity 0.3s ease, transform 0.3s ease';
        messageContainer.style.opacity = '0';
        messageContainer.style.transform = 'translateY(-20px)';
        
        setTimeout(() => {
            messageContainer.style.display = 'none';
        }, 300);
    }, 4000);
}

// Función para actualizar la visualización de registros (barra clickeable)
function updateLogsDisplay() {
    const logsList = document.getElementById('logsList');
    if (!logsList) {
        return;
    }
    
    // Limpiar contenido anterior
    logsList.innerHTML = '';
    
    if (fanScheduleRecords.length === 0) {
        logsList.innerHTML = '<div class="logs-bar-empty" onclick="toggleLogsPopup()">No hay registros disponibles</div>';
        return;
    }
    
    // Crear barra clickeable con resumen
    const logsBar = document.createElement('div');
    logsBar.className = 'logs-bar';
    logsBar.onclick = toggleLogsPopup;
    
    const recordCount = fanScheduleRecords.length;
    const latestRecord = fanScheduleRecords[fanScheduleRecords.length - 1];
    const dayFormatted = latestRecord.day.charAt(0).toUpperCase() + latestRecord.day.slice(1);
    
    logsBar.innerHTML = `
        <div class="logs-bar-content">
            <span class="logs-bar-icon">📋</span>
            <span class="logs-bar-text">
                <span class="logs-bar-count">${recordCount} registro${recordCount > 1 ? 's' : ''}</span>
                <span class="logs-bar-latest">Último: ${dayFormatted} ${latestRecord.time} - ${latestRecord.speed}%</span>
            </span>
            <span class="logs-bar-arrow">▼</span>
        </div>
    `;
    
    logsList.appendChild(logsBar);
}

// Función para mostrar/ocultar el popup de registros
function toggleLogsPopup() {
    const popup = document.getElementById('logsPopup');
    
    if (!popup) {
        createLogsPopup();
        return;
    }
    
    if (popup.style.display === 'none' || popup.style.display === '') {
        showLogsPopup();
    } else {
        hideLogsPopup();
    }
}

// Función para crear el popup de registros
function createLogsPopup() {
    const popup = document.createElement('div');
    popup.id = 'logsPopup';
    popup.className = 'logs-popup';
    
    const popupContent = document.createElement('div');
    popupContent.className = 'logs-popup-content';
    
    const popupHeader = document.createElement('div');
    popupHeader.className = 'logs-popup-header';
    popupHeader.innerHTML = `
        <h3 class="logs-popup-title">Registros de Horario</h3>
        <button class="logs-popup-close" onclick="hideLogsPopup()">×</button>
    `;
    
    const popupList = document.createElement('div');
    popupList.id = 'logsPopupList';
    popupList.className = 'logs-popup-list';
    
    const popupFooter = document.createElement('div');
    popupFooter.id = 'logsPopupFooter';
    popupFooter.className = 'logs-popup-footer';
    
    popupContent.appendChild(popupHeader);
    popupContent.appendChild(popupList);
    popupContent.appendChild(popupFooter);
    popup.appendChild(popupContent);
    
    document.body.appendChild(popup);
    
    // Cerrar al hacer clic fuera del popup
    popup.addEventListener('click', function(e) {
        if (e.target === popup) {
            hideLogsPopup();
        }
    });
    
    updateLogsPopupContent();
    updateLogsPopupFooter();
    showLogsPopup();
}

// Función para actualizar el contenido del popup
function updateLogsPopupContent() {
    const popupList = document.getElementById('logsPopupList');
    if (!popupList) {
        return;
    }
    
    popupList.innerHTML = '';
    
    if (fanScheduleRecords.length === 0) {
        popupList.innerHTML = '<p class="logs-popup-empty">No hay registros disponibles</p>';
        return;
    }
    
    // Obtener los últimos 5 registros (más recientes primero)
    const sortedRecords = [...fanScheduleRecords].reverse();
    const last5Records = sortedRecords.slice(0, 5);
    
    last5Records.forEach((record, index) => {
        const logItem = document.createElement('div');
        logItem.className = 'logs-popup-item';
        
        // Agregar clase selected si este registro está seleccionado
        if (selectedScheduleRecord && selectedScheduleRecord.id === record.id) {
            logItem.classList.add('selected');
        }
        
        // Agregar evento de clic para seleccionar
        logItem.onclick = function() {
            selectScheduleRecord(record);
        };
        
        // Formatear el día con primera letra mayúscula
        const dayFormatted = record.day.charAt(0).toUpperCase() + record.day.slice(1);
        
        logItem.innerHTML = `
            <div class="logs-popup-item-number">${index + 1}</div>
            <div class="logs-popup-item-content">
                <div class="logs-popup-item-header">
                    <span class="logs-popup-item-day">${dayFormatted}</span>
                    <span class="logs-popup-item-time">${record.time}</span>
                </div>
                <div class="logs-popup-item-details">
                    <span class="logs-popup-item-speed">Velocidad: ${record.speed}%</span>
                    <span class="logs-popup-item-date">${record.timestamp}</span>
                </div>
            </div>
            ${selectedScheduleRecord && selectedScheduleRecord.id === record.id ? '<div class="logs-popup-item-check">✓</div>' : ''}
        `;
        
        popupList.appendChild(logItem);
    });
    
    // Actualizar el footer después de actualizar el contenido
    updateLogsPopupFooter();
}

// Función para seleccionar un registro
function selectScheduleRecord(record) {
    // Si se hace clic en el mismo registro, deseleccionar
    if (selectedScheduleRecord && selectedScheduleRecord.id === record.id) {
        selectedScheduleRecord = null;
    } else {
        selectedScheduleRecord = record;
    }
    
    // Actualizar la visualización
    updateLogsPopupContent();
    updateLogsPopupFooter();
    
    console.log('[FAN SCHEDULE] Registro seleccionado:', selectedScheduleRecord);
}

// Función para actualizar el footer del popup (botón Asignar)
function updateLogsPopupFooter() {
    const popupFooter = document.getElementById('logsPopupFooter');
    if (!popupFooter) {
        return;
    }
    
    popupFooter.innerHTML = '';
    
    if (selectedScheduleRecord) {
        const assignButton = document.createElement('button');
        assignButton.className = 'logs-popup-assign-btn';
        assignButton.innerHTML = `
            <span class="logs-popup-assign-icon">✓</span>
            <span class="logs-popup-assign-label">Asignar</span>
        `;
        assignButton.onclick = assignSchedule;
        
        popupFooter.appendChild(assignButton);
        popupFooter.style.display = 'flex';
    } else {
        popupFooter.style.display = 'none';
    }
}

// Función para asignar el registro seleccionado
function assignSchedule() {
    if (!selectedScheduleRecord) {
        console.warn('[FAN SCHEDULE] No hay registro seleccionado');
        return;
    }
    
    console.log('[FAN SCHEDULE] Asignando registro:', selectedScheduleRecord);
    
    // Aquí se implementará la lógica para enviar las instrucciones al ESP32
    // Por ahora solo mostramos un mensaje en consola
    console.log(`[FAN SCHEDULE] Instrucciones a enviar:
        - Día: ${selectedScheduleRecord.day}
        - Hora: ${selectedScheduleRecord.time}
        - Velocidad: ${selectedScheduleRecord.speed}%`);
    
    // TODO: Implementar envío de instrucciones al ESP32
    // Ejemplo:
    // fetch('/fan/schedule/assign', {
    //     method: 'POST',
    //     headers: { 'Content-Type': 'application/json' },
    //     body: JSON.stringify({
    //         day: selectedScheduleRecord.day,
    //         time: selectedScheduleRecord.time,
    //         speed: selectedScheduleRecord.speed
    //     })
    // })
    // .then(resp => resp.text())
    // .then(data => {
    //     console.log('[FAN SCHEDULE] Respuesta del servidor:', data);
    //     showScheduleConfirmationMessage('Horario asignado correctamente');
    // })
    // .catch(err => {
    //     console.error('[FAN SCHEDULE] Error al asignar:', err);
    // });
    
    // Cerrar el popup automáticamente antes de mostrar el mensaje
    hideLogsPopup();
    
    // Mostrar mensaje de confirmación después de cerrar el popup
    setTimeout(() => {
        showScheduleConfirmationMessage('Horario asignado correctamente');
    }, 350); // Esperar a que termine la animación de cierre
}

// Función para mostrar el popup
function showLogsPopup() {
    const popup = document.getElementById('logsPopup');
    if (!popup) {
        createLogsPopup();
        return;
    }
    
    // Resetear selección al abrir el popup
    selectedScheduleRecord = null;
    
    updateLogsPopupContent();
    updateLogsPopupFooter();
    
    popup.style.display = 'flex';
    popup.style.opacity = '0';
    
    setTimeout(() => {
        popup.style.transition = 'opacity 0.3s ease';
        popup.style.opacity = '1';
        
        const content = popup.querySelector('.logs-popup-content');
        if (content) {
            content.style.transform = 'scale(0.9) translateY(-20px)';
            setTimeout(() => {
                content.style.transition = 'transform 0.3s ease';
                content.style.transform = 'scale(1) translateY(0)';
            }, 10);
        }
    }, 10);
}

// Función para ocultar el popup
function hideLogsPopup() {
    const popup = document.getElementById('logsPopup');
    if (!popup) {
        return;
    }
    
    // Resetear selección al cerrar
    selectedScheduleRecord = null;
    
    const content = popup.querySelector('.logs-popup-content');
    if (content) {
        content.style.transition = 'transform 0.3s ease';
        content.style.transform = 'scale(0.9) translateY(-20px)';
    }
    
    popup.style.transition = 'opacity 0.3s ease';
    popup.style.opacity = '0';
    
    setTimeout(() => {
        popup.style.display = 'none';
    }, 300);
}

// ========== FUNCIONES DEL MODO TEMPERATURA ==========

// Función para iniciar el modo temperatura
function startTemperatureMode() {
    // Si ya hay un intervalo activo, no crear otro
    if (fanTemperatureInterval !== null) {
        return;
    }
    
    console.log('[FAN TEMPERATURE] Modo temperatura activado');
    
    // Optimización para móvil: intervalo más largo para ahorrar batería
    // Desktop: 1 segundo, Móvil: 2 segundos
    const updateInterval = isMobileDevice() ? 2000 : 1000;
    
    // Primera lectura inmediata
    updateTemperatureForFan();
    
    // Monitorear temperatura periódicamente
    fanTemperatureInterval = setInterval(() => {
        if (!fanTemperatureMode) {
            stopTemperatureMode();
            return;
        }
        
        updateTemperatureForFan();
    }, updateInterval);
}

// Función auxiliar para actualizar temperatura (reutilizable)
function updateTemperatureForFan() {
    // No actualizar si la página no está visible (optimización móvil)
    if (!isPageVisible) {
        return;
    }
    
    // Obtener temperatura actual
    fetch("/temperature")
        .then(resp => {
            if (!resp.ok) {
                if (resp.status === 401) {
                    return Promise.reject(new Error("Unauthorized"));
                }
                throw new Error(`HTTP ${resp.status}`);
            }
            return resp.text().then(text => {
                try {
                    return JSON.parse(text);
                } catch (e) {
                    console.error("[FAN TEMP] Error parseando JSON:", text);
                    throw e;
                }
            });
        })
        .then(data => {
            if (data && typeof data.temperature === 'number' && isFinite(data.temperature)) {
                const temp = data.temperature;
                controlFanByTemperature(temp);
            }
        })
        .catch(err => {
            if (err.message !== "Unauthorized") {
                console.error("[FAN TEMP] Error obteniendo temperatura:", err);
            }
        });
}

// Función para detener el modo temperatura
function stopTemperatureMode() {
    if (fanTemperatureInterval !== null) {
        clearInterval(fanTemperatureInterval);
        fanTemperatureInterval = null;
        console.log('[FAN TEMPERATURE] Modo temperatura desactivado');
    }
}

// Función para controlar el ventilador basado en la temperatura
function controlFanByTemperature(temperature) {
    let newSpeed = 0;
    
    if (temperature > 45.0) {
        // Si supera 45°C, encender al 100%
        newSpeed = 100;
    } else if (temperature > 30.0) {
        // Si supera 30°C, encender al 40%
        newSpeed = 40;
    } else {
        // Si está por debajo de 30°C, apagar
        newSpeed = 0;
    }
    
    // Optimización: solo actualizar si hay cambio significativo (evitar actualizaciones innecesarias)
    // En móvil, usar umbral más grande para reducir actualizaciones
    const speedThreshold = isMobileDevice() ? 5 : 1;
    const speedChanged = Math.abs(newSpeed - fanCurrentSpeed) >= speedThreshold;
    
    // Si el ventilador ya estaba encendido y la temperatura aumenta,
    // aumentar la velocidad. Si la temperatura baja, ajustar según la nueva temperatura.
    if (speedChanged) {
        const previousSpeed = fanCurrentSpeed;
        fanCurrentSpeed = newSpeed;
        
        if (newSpeed > previousSpeed) {
            console.log(`[FAN TEMP] Temperatura: ${temperature.toFixed(1)}°C - Aumentando velocidad de ${previousSpeed}% a ${newSpeed}%`);
        } else if (newSpeed < previousSpeed && newSpeed > 0) {
            console.log(`[FAN TEMP] Temperatura: ${temperature.toFixed(1)}°C - Reduciendo velocidad de ${previousSpeed}% a ${newSpeed}%`);
        } else if (newSpeed > 0) {
            console.log(`[FAN TEMP] Temperatura: ${temperature.toFixed(1)}°C - Encendiendo ventilador a ${newSpeed}%`);
        } else {
            console.log(`[FAN TEMP] Temperatura: ${temperature.toFixed(1)}°C - Apagando ventilador`);
        }
        
        // Actualizar estado visual solo cuando hay cambio
        updateFanTemperatureStatus(temperature, fanCurrentSpeed);
        
        // Aquí puedes agregar lógica para enviar el comando al ESP32
        // fetch(`/fan/temperature?speed=${fanCurrentSpeed}`)
        //     .then(resp => resp.text())
        //     .then(data => console.log(data))
        //     .catch(err => console.error(err));
    } else {
        // Actualizar solo la temperatura en el estado (sin cambiar velocidad)
        updateFanTemperatureStatus(temperature, fanCurrentSpeed, true);
    }
}

// Función para actualizar el estado visual del modo temperatura
function updateFanTemperatureStatus(temperature, speed, temperatureOnly = false) {
    const statusText = document.getElementById('fanStatusText');
    if (statusText && fanTemperatureMode) {
        // Optimización para móvil: formato más compacto
        if (isMobileDevice()) {
            if (speed > 0) {
                statusText.textContent = `${temperature.toFixed(1)}°C | ${speed}%`;
            } else {
                statusText.textContent = `${temperature.toFixed(1)}°C | OFF`;
            }
        } else {
            // Formato completo para desktop
            if (speed > 0) {
                statusText.textContent = `Temperatura: ${temperature.toFixed(1)}°C - ${speed}%`;
            } else {
                statusText.textContent = `Temperatura: ${temperature.toFixed(1)}°C - Apagado`;
            }
        }
        
        // Agregar clase para indicar estado activo
        if (speed > 0) {
            statusText.classList.add('fan-temp-active');
        } else {
            statusText.classList.remove('fan-temp-active');
        }
    }
}

// Inicializar la visualización de registros al cargar
if (document.getElementById('logsList')) {
    updateLogsDisplay();
}

// Optimización: Pausar monitoreo cuando la página no está visible (ahorro de batería en móvil)
let isPageVisible = true;
document.addEventListener('visibilitychange', function() {
    isPageVisible = !document.hidden;
    
    if (fanTemperatureMode) {
        if (isPageVisible) {
            // Reanudar monitoreo si estaba activo
            if (fanTemperatureInterval === null) {
                startTemperatureMode();
            }
        } else {
            // Pausar monitoreo cuando la página no está visible
            stopTemperatureMode();
        }
    }
});

// Limpiar recursos al salir de la página
window.addEventListener('beforeunload', function() {
    stopTemperatureMode();
});

// Inicializar funcionalidad del slider cuando la página carga
if (document.getElementById('sliderClock')) {
    // Actualizar reloj cada segundo
    setInterval(updateSliderClock, 1000);
    updateSliderClock();
    
    // Actualizar temperatura cada segundo
    setInterval(updateSliderTemperature, 1000);
    updateSliderTemperature();
    
    console.log("[SLIDER] Funcionalidad del slider inicializada");
}

// Función de logout
function doLogout() {
    fetch("/logout").then(() => { 
        window.location.href = "/"; 
    }).catch(() => {
        window.location.href = "/";
    });
}