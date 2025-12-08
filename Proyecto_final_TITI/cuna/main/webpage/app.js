/**
 * Add globals here
 */
var seconds = null;
var otaTimerVar = null;
var wifiConnectInterval = null;

/**
 * Initialize functions here.
 */
$(document).ready(function(){
	//getUpdateStatus();
	startDHTSensorInterval();
	startDateTimeInterval(); 

	// Animación genérica para todos los botones .btn
	$(".btn").on("click", function() {
		const $btn = $(this);

		// evita acumular la clase si se spammea el click
		$btn.removeClass("is-pressed");

		// forza reflow para reiniciar la animación
		void this.offsetWidth;

		$btn.addClass("is-pressed");

		// quita la clase al terminar la animación
		setTimeout(function() {
			$btn.removeClass("is-pressed");
		}, 200);
	});

	// Lógica específica que ya tenías
	$("#connect_wifi").on("click", function(){
		checkCredentials();
	}); 
});

/**
 * Gets file name and size for display on the web page.
 */        
function getFileInfo() 
{
    var x = document.getElementById("selected_file");
    var file = x.files[0];

    document.getElementById("file_info").innerHTML = "<h4>File: " + file.name + "<br>" + "Size: " + file.size + " bytes</h4>";
}

/**
 * Handles the firmware update.
 */
function updateFirmware() 
{
    // Form Data
    var formData = new FormData();
    var fileSelect = document.getElementById("selected_file");
    
    if (fileSelect.files && fileSelect.files.length == 1) 
	{
        var file = fileSelect.files[0];
        formData.set("file", file, file.name);
        document.getElementById("ota_update_status").innerHTML = "Uploading " + file.name + ", Firmware Update in Progress...";

        // Http Request
        var request = new XMLHttpRequest();

        request.upload.addEventListener("progress", updateProgress);
        request.open('POST', "/OTAupdate");
        request.responseType = "blob";
        request.send(formData);
    } 
	else 
	{
        window.alert('Select A File First')
    }
}

/**
 * Progress on transfers from the server to the client (downloads).
 */
function updateProgress(oEvent) 
{
    if (oEvent.lengthComputable) 
	{
        getUpdateStatus();
    } 
	else 
	{
        window.alert('total size is unknown')
    }
}

/**
 * Posts the firmware udpate status.
 */
function getUpdateStatus() 
{
    var xhr = new XMLHttpRequest();
    var requestURL = "/OTAstatus";
    xhr.open('POST', requestURL, false);
    xhr.send('ota_update_status');

    if (xhr.readyState == 4 && xhr.status == 200) 
	{		
        var response = JSON.parse(xhr.responseText);
						
	 	document.getElementById("latest_firmware").innerHTML = response.compile_date + " - " + response.compile_time

		// If flashing was complete it will return a 1, else -1
		// A return of 0 is just for information on the Latest Firmware request
        if (response.ota_update_status == 1) 
		{
    		// Set the countdown timer time
            seconds = 10;
            // Start the countdown timer
            otaRebootTimer();
        } 
        else if (response.ota_update_status == -1)
		{
            document.getElementById("ota_update_status").innerHTML = "!!! Upload Error !!!";
        }
    }
}

/**
 * Displays the reboot countdown.
 */
function otaRebootTimer() 
{	
    document.getElementById("ota_update_status").innerHTML = "OTA Firmware Update Complete. This page will close shortly, Rebooting in: " + seconds;

    if (--seconds == 0) 
	{
        clearTimeout(otaTimerVar);
        window.location.reload();
    } 
	else 
	{
        otaTimerVar = setTimeout(otaRebootTimer, 1000);
    }
}


/**
 * Gets DHT22 sensor temperature and humidity values for display on the web page.
 */

function getregValues() {
    fetch("/api/registers")
    .then(res => res.json())
    .then(regs => {
        for (let i = 1; i <= 10; i++) {
            const el = document.getElementById(`reg_${i}`);
            if (regs[i]) {
                el.textContent = `${regs[i].hour}:${regs[i].minute} (${regs[i].days.join(", ")})`;
            } else {
                el.textContent = "--";
            }
        }
    })
    .catch(err => alert("Error leyendo registros: " + err));
}

function getDHTSensorValues()
{
    $.getJSON('/dhtSensor.json', function(data) {

        // ----- Temperatura NTC -----
        if (data["temp"] === null || data["temp"] === undefined) {
            $("#temperature_reading").text("--");
        } else {
            // Si viene como número, mostramos con 1 decimal
            var tempVal = data["temp"];
            if (typeof tempVal === "number") {
                $("#temperature_reading").text(tempVal.toFixed(1) + " °C");
            } else {
                $("#temperature_reading").text(tempVal + " °C");
            }
        }

        // ----- Estado PIR -----
        if (data["pir"] === 1) {
            $("#pir_status").text("Movimiento detectado");
        } else {
            $("#pir_status").text("Sin movimiento");
        }
    }).fail(function() {
        $("#temperature_reading").text("--");
        $("#pir_status").text("Sin datos");
    });
}


/**
 * Sets the interval for getting the updated DHT22 sensor values.
 */

function startDHTSensorInterval() {
	setInterval(getDHTSensorValues, 5000);
}


function getCurrentDateTime()
{
    $.getJSON('/time.json', function(data) {
        if (data.date && data.time) {
            $("#current_datetime").text(data.date + " " + data.time);
        } else {
            $("#current_datetime").text("--");
        }
    }).fail(function() {
        // Si falla, no revienta la página
        $("#current_datetime").text("No time");
    });
}

function startDateTimeInterval()
{
    // Primera llamada inmediata
    getCurrentDateTime();
    // Luego actualiza cada segundo (si quieres cada 5 s, cambia 1000 -> 5000)
    setInterval(getCurrentDateTime, 1000);
}


/**
 * Clears the connection status interval.
 */
function stopWifiConnectStatusInterval()
{
	if (wifiConnectInterval != null)
	{
		clearInterval(wifiConnectInterval);
		wifiConnectInterval = null;
	}
}

/**
 * Gets the WiFi connection status.
 */
function getWifiConnectStatus()
{
	var xhr = new XMLHttpRequest();
	var requestURL = "/wifiConnectStatus";
	xhr.open('POST', requestURL, false);
	xhr.send('wifi_connect_status');
	
	if (xhr.readyState == 4 && xhr.status == 200)
	{
		var response = JSON.parse(xhr.responseText);
		
		document.getElementById("wifi_connect_status").innerHTML = "Connecting...";
		
		if (response.wifi_connect_status == 2)
		{
			document.getElementById("wifi_connect_status").innerHTML = "<h4 class='rd'>Failed to Connect. Please check your AP credentials and compatibility</h4>";
			stopWifiConnectStatusInterval();
		}
		else if (response.wifi_connect_status == 3)
		{
			document.getElementById("wifi_connect_status").innerHTML = "<h4 class='gr'>Connection Success!</h4>";
			stopWifiConnectStatusInterval();
		}
	}
}

/**
 * Starts the interval for checking the connection status.
 */
function startWifiConnectStatusInterval() {
	wifiConnectInterval = setInterval(getWifiConnectStatus, 2800);
}

/**
 * Connect WiFi function called using the SSID and password entered into the text fields.
 */
function connectWifi() {
		var selectedSSID = $("#connect_ssid").val();
		var pwd = $("#connect_pass").val();

		var requestData = {
				'selectedSSID': selectedSSID,
				'pwd': pwd,
				'timestamp': Date.now()
		};

		$.ajax({
				url: '/wifiConnect.json',
				dataType: 'json',
				method: 'POST',
				cache: false,
				data: JSON.stringify(requestData),
				contentType: 'application/json',
				success: function(response) {
						console.log(response);
				},
				error: function(xhr) {
						console.error(xhr.responseText);
				}
		});
}

/**
 * Checks credentials on connect_wifi button click.
 */
function checkCredentials() {
    var selectedNumber = parseInt($("#selectNumber").val(), 10);
    var hours = parseInt($("#hours").val(), 10);
    var minutes = parseInt($("#minutes").val(), 10);

    var selectedDays = [];
    $(".days input[type='checkbox']").each(function() {
        selectedDays.push($(this).prop('checked') ? '1' : '0');
    });

    var payload = {
        register: selectedNumber,
        hour: hours,
        minute: minutes,
        days: selectedDays
    };

    fetch('/api/register', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
    })
    .then(res => res.text())
    .then(txt => {
        console.log('saved', txt);
        getregValues();
    })
    .catch(err => {
        console.error('Error saving register', err);
        alert('Error saving registro');
    });
	{
		x.type = "password";
	}
}


function send_register() {
	var selectedNumber = $("#selectNumber").val();
	var hours = $("#hours").val();
	var minutes = $("#minutes").val();

	var selectedDays = [];
	$(".days input[type='checkbox']").each(function() {
		selectedDays.push($(this).prop('checked') ? '1' : '0');
	});

	var requestData = {
		'selectedNumber': selectedNumber.toString(),
		'hours': hours.toString(),
		'minutes': minutes.toString(),
		'selectedDays': selectedDays,
		'timestamp': Date.now()
	};

	$.ajax({
		url: '/regchange.json',
		dataType: 'json',
		method: 'POST',
		cache: false,
		data: JSON.stringify(requestData),
		contentType: 'application/json',
		success: function(response) {
			console.log('reg saved', response);
			// Optionally refresh displayed regs
			getregValues();
		},
		error: function(xhr) {
			console.error(xhr.responseText);
			alert('Error saving registro');
		}
	});
}

/**
 * toogle led function.
 */
function read_reg()
{

	
	$.ajax({
		url: '/readreg.json',
		dataType: 'json',
		method: 'POST',
		cache: false,
		//headers: {'my-connect-ssid': selectedSSID, 'my-connect-pwd': pwd},
		//data: {'timestamp': Date.now()}
	});
//	var xhr = new XMLHttpRequest();
//	xhr.open("POST", "/toogle_led.json");
//	xhr.setRequestHeader("Content-Type", "application/json");
//	xhr.send(JSON.stringify({data: "mi información"}));
}


function erase_register() {
    var selectedNumber = document.getElementById('selectNumber').value;
    fetch('/api/register/' + encodeURIComponent(selectedNumber), {
        method: 'DELETE'
    })
    .then(res => res.text())
    .then(txt => {
        console.log('deleted', txt);
        document.getElementById('reg_' + selectedNumber).textContent = '--';
    })
    .catch(err => {
        console.error('Error deleting register', err);
        alert('Error borrando registro');
    });
}

function toogle_led() 
{	
	$.ajax({
		url: '/toogle_led.json',
		dataType: 'json',
		method: 'POST',
		cache: false,
	});

}

function brigthness_up() 
{	
	$.ajax({
		url: '/toogle_led.json',
		dataType: 'json',
		method: 'POST',
		cache: false,
	});

}


// Actualizar el valor del slider en texto
$(document).ready(function() {
    const $range = $("#fan_speed");
    const $label = $("#fan_speed_value");

    $label.text($range.val() + "%");

    $range.on("input change", function() {
        $label.text($(this).val() + "%");
    });
});

// Enviar modo + velocidad al ESP32
function apply_fan_control() {
    const mode  = $("#fan_mode").val();   // "manual", "auto", "registros"
    const speed = parseInt($("#fan_speed").val(), 10);

    const payload = JSON.stringify({
        mode: mode,
        speed: speed
    });

    $.ajax({
        url: "/fanControl.json",
        dataType: "json",
        method: "POST",
        cache: false,
        data: payload,
        contentType: "application/json",
        success: function(resp) {
            $("#fan_status").text(resp.status || "Configuración aplicada");
        },
        error: function(xhr, status, error) {
            console.error(xhr.responseText);
            $("#fan_status").text("Error al aplicar configuración");
        }
    });
}
// ========================
//   ENVIAR REGISTRO
// ========================
function send_register() {
    const regNumber = document.getElementById("selectNumber").value;
    const hour = document.getElementById("hours").value;
    const minutes = document.getElementById("minutes").value;

    // Días seleccionados
    const days = [];
    document.querySelectorAll(".days input[type='checkbox']").forEach(item => {
        if (item.checked) days.push(item.value);
    });

    // Estructura JSON a enviar
    const data = {
        register: parseInt(regNumber),
        hour: parseInt(hour),
        minute: parseInt(minutes),
        days: days
    };

    fetch("/api/register", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(data)
    })
    .then(res => res.text())
    .then(txt => alert("Registro guardado: " + txt))
    .catch(err => alert("Error enviando registro: " + err));
}



// End of file: only keep single implementations for register CRUD aligned with server










    










    


