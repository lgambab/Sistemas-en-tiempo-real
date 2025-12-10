/**
 * @file app.js
 * @brief Aplicación web JavaScript para control del sistema de cuna inteligente
 * @author Jair Hernan Telpis Cuaran, Luis Fernando Gamba Bedoya
 * @date 2025
 * @version 1.0.0
 * 
 * @details Interfaz web que proporciona:
 * - Control de ventilador (manual/auto/registros)
 * - Gestión de registros programados (CRUD)
 * - Monitoreo de sensores (temperatura NTC, PIR)
 * - Configuración WiFi
 * - Actualización OTA de firmware
 * - Sincronización de fecha/hora
 * 
 * **Endpoints REST consumidos:**
 * - GET  /dhtSensor.json → Leer temperatura y PIR
 * - POST /fanSettings.json → Configurar ventilador
 * - POST /regchange.json → Modificar registro
 * - POST /regerase.json → Eliminar registro
 * - GET  /readreg.json → Leer todos los registros
 * - POST /OTAupdate → Actualizar firmware
 * - POST /wifiConnect.json → Conectar a WiFi
 * 
 * Universidad Nacional de Colombia - Curso RTOS
 */

/**
 * @var seconds
 * @brief Contador de segundos para el temporizador de reinicio OTA
 */
var seconds 	= null;

/**
 * @var otaTimerVar
 * @brief Handle del temporizador de reinicio tras actualización OTA
 */
var otaTimerVar =  null;

/**
 * @var wifiConnectInterval
 * @brief Intervalo para verificar estado de conexión WiFi
 */
var wifiConnectInterval = null;

/**
 * @brief Inicialización de la aplicación web
 * @details Se ejecuta cuando el DOM está completamente cargado. Inicializa:
 * - Intervalos de actualización de sensores (temperatura, PIR)
 * - Intervalos de sincronización de fecha/hora
 * - Event handlers para botones (animaciones, WiFi, ventilador, registros)
 * - Animaciones CSS para feedback visual
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
 * @brief Obtener información del archivo seleccionado para OTA
 * @details Lee el archivo seleccionado en el input file y muestra
 * su nombre y tamaño en bytes en el elemento HTML #file_info
 */
function getFileInfo() 
{
    var x = document.getElementById("selected_file");
    var file = x.files[0];

    document.getElementById("file_info").innerHTML = "<h4>File: " + file.name + "<br>" + "Size: " + file.size + " bytes</h4>";
}

/**
 * @brief Ejecutar actualización de firmware via OTA
 * @details Envía el archivo .bin seleccionado al ESP32 mediante POST a /OTAupdate.
 * Muestra progreso de carga y maneja la respuesta del servidor.
 * 
 * @note El ESP32 debe reiniciarse automáticamente tras aplicar el nuevo firmware
 * @warning Solo acepta archivos .bin válidos de ESP-IDF
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
 * @brief Actualizar barra de progreso durante carga OTA
 * @param oEvent Evento de progreso con información de bytes transferidos
 * @details Callback para XMLHttpRequest.upload.addEventListener("progress")
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
 * @brief Consultar estado de actualización OTA
 * @details Realiza POST a /OTAstatus y procesa la respuesta JSON:
 * - ota_update_status = 1: Actualización exitosa, inicia temporizador de reinicio
 * - ota_update_status = -1: Error en la actualización
 * - ota_update_status = 0: Información sobre firmware actual
 * 
 * @note También actualiza #latest_firmware con fecha/hora de compilación
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
 * @brief Temporizador de cuenta regresiva para reinicio tras OTA
 * @details Muestra contador de 10 segundos antes de recargar la página.
 * Permite que el ESP32 reinicie y aplique el nuevo firmware.
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


/**
 * @brief Leer valores de registros programados
 * @details Realiza GET a /readreg.json y actualiza elementos HTML #reg_1 a #reg_10
 * con los datos de cada registro. Muestra "--" si no hay datos.
 * 
 * @note Usado al cargar la página para mostrar registros existentes
 */
function getregValues()
{
    $.getJSON('/readreg.json', function(data) {
        $("#reg_1").text(data["reg1"] || "--");
        $("#reg_2").text(data["reg2"] || "--");
        $("#reg_3").text(data["reg3"] || "--");
        $("#reg_4").text(data["reg4"] || "--");
        $("#reg_5").text(data["reg5"] || "--");
        $("#reg_6").text(data["reg6"] || "--");
        $("#reg_7").text(data["reg7"] || "--");
        $("#reg_8").text(data["reg8"] || "--");
        $("#reg_9").text(data["reg9"] || "--");
        $("#reg_10").text(data["reg10"] || "--");
    }).fail(function(jq, status, err) {
        console.error("Error leyendo registros:", status, err);
    });
}


/**
 * @brief Leer valores de sensores (temperatura NTC y PIR)
 * @details Realiza GET a /dhtSensor.json y actualiza:
 * - #temperature_reading: Temperatura en °C del sensor NTC
 * - #pir_status: Estado del sensor PIR (movimiento detectado / sin movimiento)
 * 
 * Maneja valores nulos y errores de lectura mostrando "--" o "Sin datos"
 */
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
 * @brief Iniciar intervalo de actualización de sensores
 * @details Ejecuta getDHTSensorValues() cada 5 segundos para mantener
 * actualizada la lectura de temperatura NTC y estado del PIR
 */
function startDHTSensorInterval()
{
	setInterval(getDHTSensorValues, 5000);    
}

/**
 * @brief Obtener fecha y hora actual del ESP32
 * @details Realiza GET a /time.json y actualiza el elemento #current_datetime.
 * Utiliza la hora sincronizada por SNTP del sistema.
 */
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

/**
 * @brief Iniciar intervalo de actualización de fecha/hora
 * @details Ejecuta getCurrentDateTime() inmediatamente y luego cada 1 segundo
 * para mantener sincronizado el reloj en la interfaz web
 */
function startDateTimeInterval()
{
    // Primera llamada inmediata
    getCurrentDateTime();
    // Luego actualiza cada segundo (si quieres cada 5 s, cambia 1000 -> 5000)
    setInterval(getCurrentDateTime, 1000);
}


/**
 * @brief Detener intervalo de verificación de conexión WiFi
 * @details Limpia el intervalo wifiConnectInterval para dejar de consultar
 * el estado de conexión una vez que se conectó o falló
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
 * @brief Consultar estado de conexión WiFi
 * @details Realiza POST a /wifiConnectStatus y actualiza #wifi_connect_status con:
 * - Estado 2: Falló la conexión (credenciales incorrectas o incompatibilidad)
 * - Estado 3: Conexión exitosa
 * 
 * Detiene el intervalo de consulta cuando se alcanza un estado final
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
 * @brief Iniciar intervalo de verificación de conexión WiFi
 * @details Ejecuta getWifiConnectStatus() cada 2.8 segundos durante
 * el proceso de conexión para actualizar el estado en tiempo real
 */
function startWifiConnectStatusInterval()
{
	wifiConnectInterval = setInterval(getWifiConnectStatus, 2800);
}

/**
 * @brief Conectar ESP32 a red WiFi
 * @details Envía SSID y contraseña al endpoint /wifiConnect.json mediante POST.
 * El ESP32 intentará conectarse a la red especificada y guardará las credenciales
 * en NVS para reconexión automática tras reinicios.
 * 
 * @note Las credenciales deben ser validadas previamente con checkCredentials()
 */
function connectWifi()
{
	// Get the SSID and password
	/*selectedSSID = $("#connect_ssid").val();
	pwd = $("#connect_pass").val();
	
	$.ajax({
		url: '/wifiConnect.json',
		dataType: 'json',
		method: 'POST',
		cache: false,
		headers: {'my-connect-ssid': selectedSSID, 'my-connect-pwd': pwd},
		data: {'timestamp': Date.now()}
	});
	*/
	selectedSSID = $("#connect_ssid").val();
	pwd = $("#connect_pass").val();
	
	// Create an object to hold the data to be sent in the request body
	var requestData = {
	  'selectedSSID': selectedSSID,
	  'pwd': pwd,
	  'timestamp': Date.now()
	};
	
	// Serialize the data object to JSON
	var requestDataJSON = JSON.stringify(requestData);
	
	$.ajax({
	  url: '/wifiConnect.json',
	  dataType: 'json',
	  method: 'POST',
	  cache: false,
	  data: requestDataJSON, // Send the JSON data in the request body
	  contentType: 'application/json', // Set the content type to JSON
	  success: function(response) {
		// Handle the success response from the server
		console.log(response);
	  },
	  error: function(xhr, status, error) {
		// Handle errors
		console.error(xhr.responseText);
	  }
	});


	//startWifiConnectStatusInterval();
}

/**
 * @brief Validar credenciales WiFi antes de conectar
 * @details Verifica que SSID y contraseña no estén vacíos.
 * Si las credenciales son válidas, llama a connectWifi().
 * Si hay errores, los muestra en #wifi_connect_credentials_errors
 */
function checkCredentials()
{
	errorList = "";
	credsOk = true;
	
	selectedSSID = $("#connect_ssid").val();
	pwd = $("#connect_pass").val();
	
	if (selectedSSID == "")
	{
		errorList += "<h4 class='rd'>SSID cannot be empty!</h4>";
		credsOk = false;
	}
	if (pwd == "")
	{
		errorList += "<h4 class='rd'>Password cannot be empty!</h4>";
		credsOk = false;
	}
	
	if (credsOk == false)
	{
		$("#wifi_connect_credentials_errors").html(errorList);
	}
	else
	{
		$("#wifi_connect_credentials_errors").html("");
		connectWifi();    
	}
}

/**
 * @brief Alternar visibilidad de la contraseña WiFi
 * @details Cambia el tipo del input #connect_pass entre 'password' y 'text'
 * para permitir al usuario ver la contraseña que está ingresando
 */
function showPassword()
{
	var x = document.getElementById("connect_pass");
	if (x.type === "password")
	{
		x.type = "text";
	}
	else
	{
		x.type = "password";
	}
}
/**
 * @brief Crear o actualizar registro programado
 * @details Obtiene datos del formulario (número de registro, hora, minuto, días)
 * y los envía al endpoint REST /api/register mediante POST.
 * 
 * Los días se envían en formato abreviado: L, M, X, J, V, S, D
 * 
 * @note El registro se guarda en NVS y persiste tras reinicios
 */
function send_register()
{
    // Get form values
    var selectedNumber = parseInt($("#selectNumber").val());
    var hours = parseInt($("#hours").val());
    var minutes = parseInt($("#minutes").val());
    
    // Create array for selected days using new format (L, M, X, J, V, S, D)
    var selectedDays = [];
    if ($("#day_mon").prop("checked")) selectedDays.push("L");  // Lunes
    if ($("#day_tue").prop("checked")) selectedDays.push("M");  // Martes
    if ($("#day_wed").prop("checked")) selectedDays.push("X");  // Miércoles
    if ($("#day_thu").prop("checked")) selectedDays.push("J");  // Jueves
    if ($("#day_fri").prop("checked")) selectedDays.push("V");  // Viernes
    if ($("#day_sat").prop("checked")) selectedDays.push("S");  // Sábado
    if ($("#day_sun").prop("checked")) selectedDays.push("D");  // Domingo

    // Create data object for new REST API
    var requestData = {
        'register': selectedNumber,
        'hour': hours,
        'minute': minutes,
        'days': selectedDays
    };

    // Send to new REST API endpoint
    $.ajax({
        url: '/api/register',
        dataType: 'json',
        method: 'POST',
        cache: false,
        data: JSON.stringify(requestData),
        contentType: 'application/json',
        success: function(response) {
            console.log('Register saved successfully:', response);
            alert('Registro guardado exitosamente');
        },
        error: function(xhr, status, error) {
            console.error('Error saving register:', xhr.responseText);
            alert('Error al guardar registro');
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
/**
 * @brief Eliminar registro programado
 * @details Envía el número de registro seleccionado al endpoint /regerase.json
 * mediante POST para eliminarlo permanentemente de NVS
 * 
 * @note El registro eliminado no se puede recuperar
 */
function erase_register()
{
    // Assuming you have selectedNumber, hours, minutes variables populated from your form
    selectedNumber = $("#selectNumber").val();



    // Create an object to hold the data to be sent in the request body
    var requestData = {
        'selectedNumber': selectedNumber,
        'timestamp': Date.now()
    };

    // Serialize the data object to JSON
    var requestDataJSON = JSON.stringify(requestData);

	$.ajax({
		url: '/regerase.json',
		dataType: 'json',
		method: 'POST',
		cache: false,
		data: requestDataJSON, // Send the JSON data in the request body
		contentType: 'application/json', // Set the content type to JSON
		success: function(response) {
		  // Handle the success response from the server
		  console.log(response);
		},
		error: function(xhr, status, error) {
		  // Handle errors
		  console.error(xhr.responseText);
		}
	  });

    // Print the resulting JSON to the console (for testing)
    //console.log(requestDataJSON);
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

/**
 * @brief Aplicar configuración del ventilador
 * @details Envía modo de operación y velocidad al endpoint /fanControl.json.
 * 
 * Modos disponibles:
 * - "manual": Control directo por slider
 * - "auto": Control automático por temperatura NTC
 * - "registros": Control por registros programados
 * 
 * @note En modo auto, la velocidad del slider se usa solo como referencia
 * @note En modo registros, la velocidad se aplica cuando un registro coincide
 */
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











    










    


