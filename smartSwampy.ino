/*
This is amateur code written by binBuddha in 2015-2016.
The intent of this program is to logically control an evaporative/swamp cooler using a Particle.io photon MCU socketed into a Particle.io relay shield.
The cooler has a two-winding motor for high and low speeds, and a water circulation pump.
The relay furthest away from the photon, R1, is connected to the pump
The relay second furthest from the photon, R2, is connected using the SPDT configuration of the relay, on goes to the high speed motor winding, off connects the common to the low speed motor winding.
The relay third furthest from the photon, R2, is used as an on/off switch, connecting to the common pin of R2. This was done to make it electrically impossible to energize both motor windings simultaenously creating a fire hazard.
The relay closest to the photon is unused.
*/
//includes
#include "Adafruit_DHT.h"
#include <Adafruit_MQTT.h>
#include "Adafruit_MQTT_SPARK.h"

//definitions
#define DHTPIN 2     // what pin we're connected to
#define DHTTYPE DHT22		// DHT 22 (AM2302)

DHT dht_u(7, DHTTYPE);                  // The upstairs DHT22 is connected to digital pin 7 on the photon
DHT dht_d(2, DHTTYPE);                  // The downstairs DHT22 is connect to digital ping 2 on the photon

/************************* Adafruit.io Setup *********************************/ 
#define AIO_SERVER      "io.adafruit.com" 
#define AIO_SERVERPORT  1883                   // use 8883 for SSL 
#define AIO_USERNAME    "_YOUR_USERNAME_" 
#define AIO_KEY         "_YOUR_API_KEY_" 
/************ Global State (you don't need to change this!) ***   ***************/ 
TCPClient TheClient; 
// Setup the MQTT client class by passing in the WiFi client and MQTT server and login details. 
Adafruit_MQTT_SPARK mqtt(&TheClient,AIO_SERVER,AIO_SERVERPORT,AIO_USERNAME,AIO_KEY); 
/****************************** Feeds ***************************************/ 
// Notice MQTT paths for AIO follow the form: <username>/feeds/<feedname> 
Adafruit_MQTT_Publish Dtemp = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/Dtemp"); 
Adafruit_MQTT_Publish Utemp = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/Utemp"); 
Adafruit_MQTT_Publish Dhumidity = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/Dhumidity"); 
Adafruit_MQTT_Publish Uhumidity = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/Uhumidity"); 
Adafruit_MQTT_Publish logs = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/logs"); 



// Constants
const int comfyTempConst = 74;          // This is the lowest comfortable temp, and the cutoff point when the cooler should stop cooling.
const int comfyTempVariance = 5;        // This is the number of degrees variance from the defined const above. I.e. 8 means it's OK for the fooler to dip 8 degree below comfy temp.
const int lastActionInterval = 10;      // To prevent erratic switching by the cooler, prevent changes to cooler operation less than this many minutes
const double tempVariance = .1;         // This is the decimal percentage variance for temperature (.1 = 10%), if a sensor reading is +/- this variance, it won't be used.
const double humidityVariance = .45;    // This is the decimal percentage variance for humidity (.25 = 25%), if a sensor reading is +/- this variance, it won't be used.
const bool allergenFilter = 0;

// Cooler status vars
int _comfyTemp = comfyTempConst;        // This allows calculation of delta against the forecast high for the day
bool debug = false;                     // Verbose debug logging to the particle console toggle
bool  _pump_on = 0;                     // Is the water circulation pump running?
int  _fan_speed = 0;                    // What is the fan speed- 0=off, 1=low, 2=high
double fcast_h = 84;                    // What is the forecase high for the day
int _holdDownTimer = 0;                 // Prevent excessive cooler change operations, value is in minutes
double lastAction = 0;                  // UNIX timestamp of the last operation change of the cooler
int sensorReadCount = 0;                // Counter for sensor readings, used to count up to 5 before trying to read the sensor again
int pubCounter = 0;                     // Counter for publishing info to the particle.io dashboard
int PUMP = D3;                          // Pump relay, R1, is controlled by digital pin3 on the photon
int FANHIGHLOW = D4;                    // SPDT relay, R2, switches between the high and low motor windings on digital ping 4
int FANONOFF = D5;                      // R3, used as an ON/OFF to the common pole of R2 to energize or cut power to the common pole of R2
int sensReadMin = 0;                    // The last minute the sensors were read

double humidity_upstairs;                // Current sensor reading of the upstairs humidity from the DHT22
double humidity_upstairs_hist[10] = {0}; // Historic sensor readings of the upstairs humidity from the DHT22
bool   humidity_upstairs_outlier;       // Only discard a single outlier of the mean

double temp_upstairs;                   // Current sensor reading of the upstairs temp from the DHT22
double temp_upstairs_hist[10] = {0};    // Historic sensor readings of the upstairs temp from the DHT22
bool   temp_upstairs_outlier;           // Only discard a single outlier of the mean

double humidity_downstairs;               // Current sensor reading of the downstairs humidity from the DHT22
double humidity_downstairs_hist[10] = {0};// Historic sensor readings of the downstairs humidity from the DHT22
bool   humidity_downstairs_outlier;       // Only discard a single outlier of the mean

double temp_downstairs;                  // Current sensor reading of the downstairs temp from the DHT22
double temp_downstairs_hist[10] = {0};   // Historic sensor readings of the downstairs temp from the DHT22
bool   temp_downstairs_outlier;          // Only discard a single outlier of the mean

double humidity_upstairs_avg;            // Variable used to hold the mean of the upstairs humidity
double humidity_upstairs_avg_calc;       // Variable used to hold the sum of the upstairs humidity used to calc the mean

double temp_upstairs_avg;                // Variable used to hold the mean of the upstairs humidity
double temp_upstairs_avg_calc;           // Variable used to hold the sum of the upstairs temp used to calc the mean

double humidity_downstairs_avg;          // Variable used to hold the mean of the downstairs humidity
double humidity_downstairs_avg_calc;     // Variable used to hold the sum of the downstairs humidity used to calc the mean

double temp_downstairs_avg;              // Variable used to hold the mean of the downstairs temp
double temp_downstairs_avg_calc;         // Variable used to hold the sum of the downstairs temp used to calc the mean

double lastGotWeather   = 0;             // Unix timestamp of last time the webhook data was returned

double lastPub   = 0;                    // Unix time of last time published to particle.io's dashboard


void setup()
{
    Particle.function("relay", relayControl);               //expose the relay function to allow for manual override using IFTT / DO button
    Particle.variable("fcast_h", fcast_h);                  //expose the forecast
    Particle.variable("uTemp", temp_upstairs_hist[0]);      //expose the Upstairs Temp
    Particle.variable("dTemp", temp_downstairs_hist[0]);    //expose the Downstairs Temp
    Particle.variable("fanSpeed", _fan_speed);              //expose the Fan speed
    Particle.variable("HDT", _holdDownTimer);               //expose the Hold Down Timer
    Particle.variable("sensReadMin", sensReadMin);               //expose the Hold Down Timer
    

    // Particle.io does these 'subscribe' operations, used to scrape data from web sources. In this case, these two lines grab the forecast high for the day.
    Particle.subscribe("hook-response/forecastio_webhook", gotWeatherData, MY_DEVICES);

    //Initilize the relay control pins as output
    pinMode(PUMP, OUTPUT);
    pinMode(FANHIGHLOW, OUTPUT);
    pinMode(FANONOFF, OUTPUT);
    
    // Initialize all relays to an OFF state.
    digitalWrite(PUMP, LOW);
    _pump_on = 0;
    digitalWrite(FANHIGHLOW, LOW);
    digitalWrite(FANONOFF, LOW);

    Time.zone(-7);

    delay(1000);        // Delay 1s to let the sensors settle
    dht_u.begin();      
    dht_d.begin();
    
}


// ************************ MAIN LOOP ***************************************
void loop() {
// This loop only executres once a minute using this initial if condition. The last minute of sensor readings is stored in sensReadMin, and compared to the current time
    if (sensReadMin != Time.minute() ) {
        sensReadMin = Time.minute();
    
        // Decrement holdDownTimer
        if (_holdDownTimer>0){
            _holdDownTimer--;
        } 
        
        // If it's been more than 3 hours since the last forecast check, get it again.
        if ( (Time.now() - lastGotWeather) > ((60*60)*3)){
            Particle.publish("forecastio_webhook");
        }

        sensorReadCount = 0; // If we encounter a misread on a sensor, this tracks how many times we re-try
    	do
        {
            sensorReadCount++;
            humidity_upstairs = dht_u.getHumidity();        // This reads the sensor data into the variable, note it could be bogus
            if (isnan(humidity_upstairs)){                  // If the number read into the variable isn't a number, then it's bad
                delay(2200);                                // Wait 2 seconds before we try again
                humidity_upstairs = dht_u.getHumidity();    // In most cases a 2 seconds wait re-read the sensor just fine
            }

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

        	temp_downstairs = dht_d.getTempFarenheit();
            if (isnan(temp_downstairs)){
                delay(2200);
                temp_downstairs = dht_d.getTempFarenheit();
            }
            
    	} while ( (isnan(humidity_upstairs) || isnan(humidity_downstairs) || isnan(temp_upstairs) || isnan(temp_downstairs) ) && ( sensorReadCount < 5 ) ); // Check to see if any of the variables are NOT numbers, re-run if needed
        
        // Check if any reads failed after 5 attempts
    	if (sensorReadCount > 5) {
                        pubFlow("Failed to read from DHT sensor 5 times!");
    		return;
    	}

        /*
        The following section will keep a 10 position moving average of the sensor data. 
        The previous section checked for totally, non-numeric data; however, we could get a strange numeric reading that just couldn't be valid.
        This section compares the incoming values to the average of the past 10, and may reject inclusion of the latest reading if the value
        varies more than the percentage as configured in the tempVariance and humidityVariance constants.
        */
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
    
    
    	// Insert latest readings into slot [0] of the hist arrays if within variance or if a previously detected outlier was set
    	// If a reading is outside variance toss it, log to the dashboard, and set outlier to true
    	
    	// Humidity
    	if ( humidity_upstairs_outlier || ( (humidity_upstairs / humidity_upstairs_avg) > (1-humidityVariance) && (humidity_upstairs / humidity_upstairs_avg) < (1+humidityVariance) ) ){
            humidity_upstairs_hist[0] = humidity_upstairs;
    	    humidity_upstairs_outlier = false;
    	}else{
            if (debug){
                pubFlow("humidity_upstairs outside mean: " + String(humidity_upstairs) + ". mean: " + String(humidity_upstairs_avg));
            }
            humidity_upstairs_outlier = true;
    	}
    
    	if ( humidity_downstairs_outlier || ( (humidity_downstairs / humidity_downstairs_avg) > (1-humidityVariance) && (humidity_downstairs / humidity_downstairs_avg) < (1+humidityVariance) ) ){
    	    humidity_downstairs_hist[0] = humidity_downstairs;
    	    humidity_downstairs_outlier = false;
    	}else{
            if (debug){
                pubFlow("humidity_downstairs outside mean: " + String(humidity_downstairs) + ". mean: " + String(humidity_downstairs_avg));
            }
            humidity_downstairs_outlier = true;
    	}
    	
    	// Temp
    	if ( temp_upstairs_outlier || ( (temp_upstairs / temp_upstairs_avg) > (1-tempVariance) && (temp_upstairs / temp_upstairs_avg) < (1+tempVariance) ) ){
    	    temp_upstairs_hist[0] = temp_upstairs;
    	    temp_upstairs_outlier = false;
    	}else{
            if (debug){
                pubFlow("temp_upstairs outside mean: " + String(temp_upstairs) + ". mean: " + String(temp_upstairs_avg));
            }
            temp_upstairs_outlier = true;
    	}
    
    	if ( temp_downstairs_outlier || ( (temp_downstairs / temp_downstairs_avg) > (1-tempVariance) && (temp_downstairs / temp_downstairs_avg) < (1+tempVariance) ) ){
    	    temp_downstairs_hist[0] = temp_downstairs;
    	    temp_downstairs_outlier = false;
    	}else{
            if (debug){
                pubFlow("temp_downstairs outside mean: " + String(temp_downstairs) + ". mean: " + String(temp_downstairs_avg));
            }
            temp_downstairs_outlier = true;
    	}
    	
    	/* **************** Cooler control logic *************************
    	The following section makes logic decisions that control the high voltage relays.
    	*/
        
        // If it's been more than .5 hour since the last cooler operation, and upstairs < downstairs, and downstairs > comfyTemp, then COOLLOW
        if ( ( (Time.now() - lastAction) > (60*30)) && temp_upstairs_hist[0] < temp_downstairs_hist[0] && temp_downstairs_hist[0] > (_comfyTemp - 10) && _holdDownTimer == 0 ){
            if (debug){
                pubFlow("It's cooler outside than downstairs, U=" + String(temp_upstairs_hist[0]) + "D=" + String(temp_downstairs_hist[0]) + "CT=" + String(_comfyTemp) );
            }
            relayControl("COOLLOW");
        }

        // If the temp drops below the comfortable temperature, turn the bloody thing off.
        if  ( temp_upstairs_hist[0] < _comfyTemp-comfyTempVariance || temp_downstairs_hist[0] < (_comfyTemp-comfyTempVariance) ) {
            if (debug){
                pubFlow("temp up/down < _comfyTemp-comfyTempVariance");
            }
            if (_fan_speed > 0 && _holdDownTimer == 0) {
                relayControl("PUMP");
                pubFlow("Brr, Utmp is:" + String(temp_upstairs_hist[0]).substring(0,4) + "(" + String(temp_upstairs_avg).substring(0,4) + ") Dtmp is:" + String(temp_downstairs_hist[0]).substring(0,4) + "(" + String(temp_downstairs_avg).substring(0,4) + ")"  );
            } else {
                if (debug){
                    pubFlow("Almost went Brr, but FS wasn't > 0 or HDT !=0");
                }
            }
        } else {
            if (debug){
                pubFlow("temp up/down is higher than _comfyTemp");
            }
            if (temp_upstairs_hist[0] > _comfyTemp && _holdDownTimer == 0) {
                if (debug){
                    pubFlow("temp up > _comfyTemp && HDT==0");
                }
                if (allergenFilter || temp_upstairs_hist[0] - _comfyTemp > 5){
                    if ( _fan_speed  < 2 ){
                        relayControl("COOLHIGH");
                    }else{
                        if (debug){
                            pubFlow("COOLHIGH needed, but already running");
                        }
                    }
                }else{
                    if ( _fan_speed != 1 ){
                        relayControl("COOLLOW");
                    } else {
                        if (debug){
                            pubFlow("COOLLOW needed, but already running");
                        }
                    }
                }
            } else {
                if (_holdDownTimer > 0){
                    if (debug){
                        pubFlow("Hold down timer in effect");
                    }
                } else {
                    if (debug){
                       pubFlow("No hold down nor change to cooler operation.");
                    }
                }
            }
        }
        
        // Update Adafruit.io with the latest data from the sensors
        if( mqtt.Update() ){ 
            Utemp.publish(temp_upstairs_hist[0]); 
            Dtemp.publish(temp_downstairs_hist[0]); 
            Uhumidity.publish(humidity_upstairs_hist[0]); 
            Dhumidity.publish(humidity_downstairs_hist[0]); 
        }
    }
    
}

// ************************** Custom functions **************************************8
void pubFlow(String pub)
{
    // If it's been less than 2 seconds since a Particle.publish was sent, wait that difference. The dashboard will softban publishing if it gets more than once a second.
    double pubDiff = 0;                    
    double rightNow = Time.now();
    pubDiff = rightNow - lastPub;
    if (pubDiff < 2) {
        delay(pubDiff*1000);
    }
    // Publish to Adafruit.io
    if( mqtt.Update() ){ 
        logs.publish(pub + "-U" + String(temp_upstairs_hist[0]).substring(0,4) + "(Avg:" + String(temp_upstairs_avg).substring(0,4) + ") D" + String(temp_downstairs_hist[0]).substring(0,4) + "(Avg:" + String(temp_downstairs_avg).substring(0,4) + ") FS:" + String(_fan_speed) + " HDT:" + String(_holdDownTimer) ); 
    }
    // Publish, if it doesn't work, keep trying 1 more time.
    while(!Particle.publish(pub, "U" + String(temp_upstairs_hist[0]).substring(0,4) + "(Avg:" + String(temp_upstairs_avg).substring(0,4) + ") D" + String(temp_downstairs_hist[0]).substring(0,4) + "(Avg:" + String(temp_downstairs_avg).substring(0,4) + ") FS:" + String(_fan_speed) + " HDT:" + String(_holdDownTimer) )) {
        delay(4200);
        pubCounter++;
        if (pubCounter > 2){
            pubCounter = 0;
            break;
        }
    }

}


// Relay controls
int relayControl(String command){
    	if (debug){
            pubFlow("Received a command of: " + command);
    	}
    	
        if (command == "COOLHIGH"){
            _pump_on = 1;
            _fan_speed = 2;
            digitalWrite(FANHIGHLOW, HIGH);
            digitalWrite(FANONOFF, HIGH);
            digitalWrite(PUMP, HIGH);
            pubFlow("COOLING HIGH");
        }
        if (command == "COOLLOW"){
            _pump_on = 1;
            _fan_speed = 1;
            digitalWrite(FANHIGHLOW, LOW);
            digitalWrite(FANONOFF, HIGH);
            digitalWrite(PUMP, HIGH);
            pubFlow("COOLING LOW");
        }
        
        if (command == "FANHIGH"){
            _pump_on = 1;
            _fan_speed = 2;
            digitalWrite(FANHIGHLOW, HIGH);
            digitalWrite(FANONOFF, HIGH);
            digitalWrite(PUMP, LOW);
            pubFlow("FAN HIGH");
        }

        if (command == "FANLOW"){
            _fan_speed = 1;
            _pump_on = 1;
            digitalWrite(FANHIGHLOW, LOW);
            digitalWrite(FANONOFF, HIGH);
            digitalWrite(PUMP, LOW);
            pubFlow("FAN LOW");
        }

        if (command == "PUMP"){
            _fan_speed = 0;
            _pump_on = 0;
            digitalWrite(FANHIGHLOW, LOW);
            digitalWrite(FANONOFF, LOW);
            digitalWrite(PUMP, HIGH);
            pubFlow("PUMP");
        }

        if (command == "OFF"){
            _fan_speed = 0;
            _pump_on = 0;
            digitalWrite(FANHIGHLOW, LOW);
            digitalWrite(FANONOFF, LOW);
            digitalWrite(PUMP, LOW);
            pubFlow("ALL OFF");
        }

        if (command == "SMART"){
            _fan_speed = 0;
            _pump_on = 0;
            digitalWrite(FANHIGHLOW, LOW);
            digitalWrite(FANONOFF, LOW);
            digitalWrite(PUMP, LOW);
            pubFlow("SMART enabled");
        }
    	
    if (command != "SMART") {
        _holdDownTimer = 60;
    }
    lastAction = Time.now();
    return 1;
}

void gotWeatherData(const char *name, const char *data) {
    String str = String(data);
    char strBuffer[125] = "";
    str.toCharArray(strBuffer, 125);
    lastGotWeather = Time.now();
    fcast_h = atof(strBuffer);
    if (fcast_h < comfyTempConst){
        _comfyTemp = comfyTempConst;
    }else{
        _comfyTemp = comfyTempConst - (fcast_h - comfyTempConst);
        if (_comfyTemp < comfyTempConst-comfyTempVariance){
            _comfyTemp = comfyTempConst-comfyTempVariance;
        }
    }
    /* JSON template used to pull the forecast info from forecastio
        {
        "event": "forecastio_webhook",
        "url": "https://api.forecast.io/forecast/YOUR API KEY HERE/YOUR GPS COORDS HERE?exclude=minutely,hourly,currently,flags",
        "requestType": "GET",
        "headers": null,
        "query": null,
        "responseTemplate": "{{#daily}}{{#data.7}}{{apparentTemperatureMax}}{{/data.7}}{{/daily}}",
        "json": null,
        "auth": null,
        "mydevices": true
    }
    */

}
