#include "Adafruit_DHT/Adafruit_DHT.h" // Using the Adafruit library to read the DHT 22 sensors
#define DHTTYPE DHT22		// DHT 22 (AM2302)
#define DHTPIN 7     // what pin we're connected to
#include "thingspeak.h" // Using the thingspeak library to log the data.

// Thingspeak API key
ThingSpeakLibrary::ThingSpeak thingspeak ("YOUR API KEY");

int  _pump_on;
int  _fan_speed = 2;
double fcast_h = 84;
int _forceControl = 0;
int _comfyTemp = 68;
int online = 1;
int sensorReadCount = 0;
int pubCounter = 0;

//Sensor Variables
float humidity_upstairs;
float humidity_upstairs_hist[10] = {0};

double temp_upstairs;
double temp_upstairs_hist[10] = {0};

float humidity_downstairs;
float humidity_downstairs_hist[10] = {0};

float temp_downstairs;
float temp_downstairs_hist[10] = {0};

float humidity_upstairs_avg;
double humidity_upstairs_avg_calc;

float temp_upstairs_avg;
double temp_upstairs_avg_calc;

float humidity_downstairs_avg;
double humidity_downstairs_avg_calc;

float temp_downstairs_avg;
double temp_downstairs_avg_calc;

double lastGotWeather   = 0;    //Unix time of last time the webhook data was returned

IPAddress remoteIP(8, 8, 8, 8); // IP address to ping to check online status

int PUMP = D3;
int FANHIGHLOW = D4;
int FANONOFF = D5;

DHT dht_u(DHTPIN, DHTTYPE);
DHT dht_d(2, DHTTYPE);

void setup()
{
    Particle.subscribe("hook-response/forecastio_webhook", gotWeatherData, MY_DEVICES);
    delay(5000);
    //Particle.publish("forecastio_webhook");

    delay(1000);        // Delay 1s to let the sensors settle
    dht_u.begin();
    delay(1000);        // Delay 1s to let the sensors settle
    dht_d.begin();

    // _lastTimeInLoop = millis();
    
    //Initilize the relay control pins as output
    pinMode(PUMP, OUTPUT);
    pinMode(FANHIGHLOW, OUTPUT);
    pinMode(FANONOFF, OUTPUT);
    // Initialize all relays to an OFF state, except the pump... the pump can run.
    digitalWrite(PUMP, HIGH);
    _pump_on = 1;
    digitalWrite(FANHIGHLOW, LOW);
    digitalWrite(FANONOFF, LOW);

    Time.zone(-7);
}

void gotWeatherData(const char *name, const char *data) {
/* JSON template used to pull the forecast info from forecastio

{
    "event": "forecastio_webhook",
    "url": "YOUR URL HERE",
    "requestType": "GET",
    "headers": null,
    "query": null,
    "responseTemplate": "{{#daily}}{{#data.7}}{{apparentTemperatureMax}}{{/data.7}}{{/daily}}",
    "json": null,
    "auth": null,
    "mydevices": true
}

*/
    String str = String(data);
    char strBuffer[125] = "";
    str.toCharArray(strBuffer, 125);
    lastGotWeather = Time.now();
    fcast_h = atof(strBuffer);
}

// relay controls
int relayControl(String command)
    {
        if (command == "COOLHIGH"){
            _pump_on = 1;
            digitalWrite(FANHIGHLOW, HIGH);
            digitalWrite(FANONOFF, HIGH);
            digitalWrite(PUMP, HIGH);
        }
        if (command == "COOLLOW"){
            _pump_on = 1;
            digitalWrite(FANHIGHLOW, LOW);
            digitalWrite(FANONOFF, HIGH);
            digitalWrite(PUMP, HIGH);
        }
        
        if (command == "FANHIGH"){
            _pump_on = 1;
            digitalWrite(FANHIGHLOW, HIGH);
            digitalWrite(FANONOFF, HIGH);
            digitalWrite(PUMP, LOW);
        }

        if (command == "FANLOW"){
            _fan_speed = 2;
            _pump_on = 1;
            digitalWrite(FANHIGHLOW, LOW);
            digitalWrite(FANONOFF, HIGH);
            digitalWrite(PUMP, LOW);
        }

        if (command == "PUMP"){
            _fan_speed = 1;
            _pump_on = 1;
            digitalWrite(FANHIGHLOW, LOW);
            digitalWrite(FANONOFF, LOW);
            digitalWrite(PUMP, HIGH);
        }

        if (command == "OFF"){
            _fan_speed = 0;
            _pump_on = 0;
            digitalWrite(FANHIGHLOW, LOW);
            digitalWrite(FANONOFF, LOW);
            digitalWrite(PUMP, LOW);
        }
    return 1;
    }

void loop()
{
    //Reset the device if network communication isn't working, based on 0 responses from 50 pings 1 second apart on the 20 minute mark of each hour.
    if(Time.minute() == 20){
        if(WiFi.ping(remoteIP) == 0) {
            online = 0;
            for (int i=1;i<=50;i++){
                delay(1000);
                if(WiFi.ping(remoteIP) != 0 ) {
                    online = 1;
                    i=50;
                    break;
                }
            }
        }
    }

    // Final attempt to log loss of Internet, if failed reset the unit.
    if(!online) {
        if(!Particle.publish("Not getting a ping reply, considering resetting")) {
            System.reset();
        }
    }

    // Skip smartness if force control is in effect.
    if (_forceControl>0){
        _forceControl--;
    } 
    
    // If it's been more than 3 hours since the last forecast check, get it again.
    if ( (Time.now() - lastGotWeather) > ((60*60)*3)){
        Particle.publish("forecastio_webhook");
    }
    
    // Reading temperature or humidity takes about 250 milliseconds, and may be up to 2 seconds old.
    sensorReadCount = 0;
	do
    {
        sensorReadCount++;
        humidity_upstairs = dht_u.getHumidity();
        if (isnan(humidity_upstairs)){
            delay(2200);
            humidity_upstairs = dht_u.getHumidity();
        }
        //humidity_downstairs = humidity_upstairs;
        
        humidity_downstairs = dht_d.getHumidity();
        if (isnan(humidity_downstairs)){
            delay(2200);
            humidity_downstairs = dht_d.getHumidity();
        }
        
    	temp_upstairs = dht_u.getTempFarenheit();
        if (isnan(temp_upstairs)){
            delay(2200);
            temp_upstairs = dht_u.getTempFarenheit();
        }
        //temp_downstairs = temp_upstairs;
        
    	temp_downstairs = dht_d.getTempFarenheit();
        if (isnan(temp_downstairs)){
            delay(2200);
            temp_downstairs = dht_d.getTempFarenheit();
        }
        
	} while ( (isnan(humidity_upstairs) || isnan(humidity_downstairs) || isnan(temp_upstairs) || isnan(temp_downstairs) ) && ( sensorReadCount < 5) );
	
    // Check if any reads failed after 5 attempts
	if (sensorReadCount > 5) {
        Particle.publish("Failed to read from DHT sensor 5 times!");
		return;
	}

    // Init the array to the same first value sensed if it's all zeros.
	if ( humidity_upstairs_hist[0] == 0 || humidity_downstairs_hist[0] == 0 || temp_upstairs_hist[0] == 0 || temp_downstairs_hist[0] == 0 ){
	    for (int i=0; i<=9; i++){
	        humidity_upstairs_hist[i] = humidity_upstairs;
	        humidity_downstairs_hist[i] = humidity_downstairs;
	        temp_upstairs_hist[i] = temp_upstairs;
	        temp_downstairs_hist[i] = temp_downstairs;
	    }
	}
	
	// Shift the last 10 readings array to make room for the latest entry
	for (int i=9; i>0; i--){
	        int y = i-1;
	        
	        humidity_upstairs_hist[i] = humidity_upstairs_hist[y];
	        humidity_downstairs_hist[i] = humidity_downstairs_hist[y];
	        temp_upstairs_hist[i] = temp_upstairs_hist[y];
	        temp_downstairs_hist[i] = temp_downstairs_hist[y];
	}
	
    //Rest calcs
    humidity_upstairs_avg_calc = 0;
    humidity_downstairs_avg_calc = 0;
    temp_upstairs_avg_calc = 0;
    temp_downstairs_avg_calc = 0;


    // Add values
	for (int i=0; i<=9; i++){
            humidity_upstairs_avg_calc = humidity_upstairs_avg_calc + humidity_upstairs_hist[i];
            humidity_downstairs_avg_calc = humidity_downstairs_avg_calc + humidity_downstairs_hist[i];
            temp_upstairs_avg_calc = temp_upstairs_avg_calc + temp_upstairs_hist[i];
            temp_downstairs_avg_calc = temp_downstairs_avg_calc + temp_downstairs_hist[i];
	}	

	// Calculate the mean of values 0-9
	humidity_upstairs_avg = humidity_upstairs_avg_calc / 10;
	humidity_downstairs_avg = humidity_downstairs_avg_calc / 10;
	temp_upstairs_avg = temp_upstairs_avg_calc / 10;
	temp_downstairs_avg = temp_downstairs_avg_calc / 10;

	// Insert latest readings into slot [0] of the hist arrays if within 15% variance
	if ( (humidity_upstairs / humidity_upstairs_avg) > .85 && (humidity_upstairs / humidity_upstairs_avg) < 1.15){
	    bool valSetUpstairsHumidity = thingspeak.recordValue(4, String(humidity_upstairs).substring(0,4));
	}else{
        Particle.publish("humidity_upstairs outside mean: " + String(humidity_upstairs) + ". mean: " + String(humidity_upstairs_avg));
	}

	if ( (humidity_downstairs / humidity_downstairs_avg) > .85 && (humidity_downstairs / humidity_downstairs_avg) < 1.15){
	    humidity_downstairs_hist[0] = humidity_downstairs;
	    bool valSetDownstairsHumidity = thingspeak.recordValue(1, String(humidity_downstairs).substring(0,4));
	}else{
        Particle.publish("humidity_downstairs outside mean: " + String(humidity_downstairs) + ". mean: " + String(humidity_downstairs_avg));
	}

	if ( (temp_upstairs / temp_upstairs_avg) > .85 && (temp_upstairs / temp_upstairs_avg) < 1.15){
	    temp_upstairs_hist[0] = temp_upstairs;
	    bool valSetUpstairsTemp = thingspeak.recordValue(5, String(temp_upstairs).substring(0,4));
	}else{
        Particle.publish("temp_upstairs outside mean: " + String(temp_upstairs) + ". mean: " + String(temp_upstairs_avg));
	}

	if ( (temp_downstairs / temp_downstairs_avg) > .85 && (temp_downstairs / temp_downstairs_avg) < 1.15){
	    temp_downstairs_hist[0] = temp_downstairs;
	    bool valSetDownstairsTemp = thingspeak.recordValue(2, String(temp_downstairs).substring(0,4));
	}else{
        Particle.publish("temp_downstairs outside mean: " + String(temp_downstairs) + ". mean: " + String(temp_downstairs_avg));
	}

    // Set latest Thingspeak non-sensor values
    bool valSetPump = thingspeak.recordValue(7, String(_forceControl));
    bool valSetFSpd = thingspeak.recordValue(8, String(_fan_speed));
    
    // Send the ThingSpeak values off
	int ThingSpeakSendCount = 0;
	bool valsSent;
	do
    {
        ThingSpeakSendCount++;
        valsSent = thingspeak.sendValues();
        if (!valsSent) {
            delay(5000);
        }
	} while (!valsSent && ThingSpeakSendCount < 5);
	
    // Check if thingspeak failed, if so try again 4 more times.
	if (ThingSpeakSendCount > 5) {
        Particle.publish("Writing to thingspeak failed =(: " + String(valsSent));
		return;
	}
    
    // If the temp drops below the comfortable temperature, turn the bloody thing off!    
    
    if (temp_upstairs < _comfyTemp && _fan_speed != 0 && _forceControl == 0) {
        relayControl("PUMP");
        Particle.publish("Temp is <_comfyTemp, currently: " + String(temp_upstairs), "turning fan off.");
        
    } else {
        if (temp_upstairs > _comfyTemp && _forceControl == 0) {
        // If the temp surpasses _comfyTemp, and the forecast is hot (>80), cool it down!   
            if (fcast_h > 80) {
                    if (temp_upstairs - _comfyTemp > 5){
                        if (!digitalRead(FANHIGHLOW)){
                            pubCounter = 0;
                            while(!Particle.publish("Temp is >_comfyTemp, fcast_h > 80. Temp is:" + String(temp_upstairs) ) ) {
                                delay(4200);
                                pubCounter++;
                                if (pubCounter > 4){
                                    break;
                                }
                            }
                            relayControl("COOLHIGH");
                        }
                    }else{
                        if (!digitalRead(FANONOFF)){

                            while(!Particle.publish("Temp is >_comfyTemp, fcast_h > 80. Temp is:" + String(temp_upstairs) ) ) {
                                delay(4200);
                                pubCounter++;
                                if (pubCounter > 4){
                                    break;
                                }
                            }
                            relayControl("COOLLOW");
                        }
                    }
                }
            }
    }
    // Wait a minute before running the loop again.
    delay(1*(60*1000));
    
}
