/*
This is amateur code written by binBuddha in 2015-2016.
The intent of this program is to logically control an evaporative/swamp cooler using a Particle.io photon MCU socketed into a Particle.io relay shield.
The cooler has a two-winding motor for high and low speeds, and a water circulation pump.
The relay further away from the photon, R1, is connected to the pump
The relay second furthest from the photon, R2, is connected using the SPDT configuration of the relay, on goes to the high speed motor winding, off connects the common to the low speed motor winding.
The relay third furthest from the photon, R2, is used as an on/off switch, connecting to the common pin of R2. This was done to make it electrically impossible to energize both motor windings simultaenously creating a fire hazard.
The relay closest to the photon is unused.
*/
//includes
#include "Adafruit_DHT/Adafruit_DHT.h" // Using the Adafruit library to read the DHT 22 sensors
#define DHTTYPE DHT22		// DHT 22 (AM2302)
#include "thingspeak.h" // Using the thingspeak library to log the data.


// These variables are unique per user/installation, and will need to be changed if you impliment this code
// This code logs to this public URL: https://thingspeak.com/channels/48967
// Thingspeak API key
ThingSpeakLibrary::ThingSpeak thingspeak ("YOUR KEY HERE");

// Constants
const int _comfyTemp = 68;              // This is the lowest comfortable temp, and the cutoff point when the cooler should stop cooling.
const int lastActionInterval = 10;      // To prevent erratic switching by the cooler, prevent changes to cooler operation less than this many minutes
const double tempVariance = .1;         // This is the decimal percentage variance for temperature (.1 = 10%), if a sensor reading is +/- this variance, it won't be used.
const double humidityVariance = .55;    // This is the decimal percentage variance for humidity (.55 = 55%), if a sensor reading is +/- this variance, it won't be used.

// Cooler status vars
bool  _pump_on;                         // Is the water circulation pump running?
int  _fan_speed = 2;                    // What is the fan speed- 0=off, 1=low, 2=high
double fcast_h = 84;                    // What is the forecase high for the day
int _holdDownTimer = 0;                 // Prevent excessive cooler change operations, value is in minutes
double lastAction = 0;                  // UNIX timestamp of the last operation change of the cooler
int sensorReadCount = 0;                // Counter for sensor readings, used to count up to 5 before trying to read the sensor again
int pubCounter = 0;                     // Counter for publishing info to the particle.io dashboard
int PUMP = D3;                          // Pump relay, R1, is controlled by digital pin3 on the photon
int FANHIGHLOW = D4;                    // SPDT relay, R2, switches between the high and low motor windings on digital ping 4
int FANONOFF = D5;                      // R3, used as an ON/OFF to the common pole of R2 to energize or cut power to the common pole of R2

//Sensor Variables
DHT dht_u(7, DHTTYPE);                  // The upstairs DHT22 is connected to digital pin 7 on the photon
DHT dht_d(2, DHTTYPE);                  // The downstairs DHT22 is connect to digital ping 2 on the photon

double humidity_upstairs;                // Current sensor reading of the upstairs humidity from the DHT22
double humidity_upstairs_hist[10] = {0}; // Historic sensor readings of the upstairs humidity from the DHT22

double temp_upstairs;                   // Current sensor reading of the upstairs temp from the DHT22
double temp_upstairs_hist[10] = {0};    // Historic sensor readings of the upstairs temp from the DHT22

double humidity_downstairs;              // Current sensor reading of the downstairs humidity from the DHT22
double humidity_downstairs_hist[10] = {0};// Historic sensor readings of the downstairs humidity from the DHT22

double temp_downstairs;                  // Current sensor reading of the downstairs temp from the DHT22
double temp_downstairs_hist[10] = {0};   // Historic sensor readings of the downstairs temp from the DHT22

double humidity_upstairs_avg;            // Variable used to hold the mean of the upstairs humidity
double humidity_upstairs_avg_calc;      // Variable used to hold the sum of the upstairs humidity used to calc the mean

double temp_upstairs_avg;                // Variable used to hold the mean of the upstairs humidity
double temp_upstairs_avg_calc;          // Variable used to hold the sum of the upstairs temp used to calc the mean

double humidity_downstairs_avg;          // Variable used to hold the mean of the downstairs humidity
double humidity_downstairs_avg_calc;    // Variable used to hold the sum of the downstairs humidity used to calc the mean

double temp_downstairs_avg;              // Variable used to hold the mean of the downstairs temp
double temp_downstairs_avg_calc;        // Variable used to hold the sum of the downstairs temp used to calc the mean

double lastGotWeather   = 0;            // Unix timestamp of last time the webhook data was returned

double lastPub   = 0;                   // Unix time of last time published to particle.io's dashboard

// This is the run-once setup for the photon
void setup()
{
    // Particle.io does these 'subscribe' operations, used to scrape data from web sources. In this case, these two lines grab the forecast high for the day.
    Particle.subscribe("hook-response/forecastio_webhook", gotWeatherData, MY_DEVICES);
    Particle.subscribe("doButtonControl", doButtonControl_handler, MY_DEVICES);

    delay(1500);        // Delay 1s to let the sensors settle
    dht_u.begin();      
    delay(1500);        // Delay 1s to let the sensors settle
    dht_d.begin();

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
    
    Particle.function("relay", relayControl); //expose the relay function to allow for manual override using IFTT / DO button
}

void pubFlow(String pub)
{
    // If it's been less than 2 seconds since a Particle.publish was sent, wait that difference. The dashboard will softban publishing if it gets more than once a second.
    double pubDiff = 0;                    
    double rightNow = Time.now();
    pubDiff = rightNow - lastPub;
    if (pubDiff < 2) {
        delay(pubDiff*1000);
    }
    // Publish, if it doesn't work, keep trying 3 more times.
    while(!Particle.publish(pub, "U" + String(temp_upstairs_hist[0]).substring(0,4) + "(Avg:" + String(temp_upstairs_avg).substring(0,4) + ") D" + String(temp_downstairs_hist[0]).substring(0,4) + "(Avg:" + String(temp_downstairs_avg).substring(0,4) + ") FS:" + String(_fan_speed) + " HDT:" + String(_holdDownTimer) )) {
        delay(4200);
        pubCounter++;
        if (pubCounter > 4){
            pubCounter = 0;
            break;
        }
    }

}

void doButtonControl_handler(const char *event, const char *data)
{
    //Whitelist for DO button operations
    if (strcmp(data,"COOLHIGH")==0) {
        _holdDownTimer = 100;
        relayControl("COOLHIGH");
    }
    if (strcmp(data,"COOLLOW")==0) {
        _holdDownTimer = 100;
        relayControl("COOLLOW");
    }
    if (strcmp(data,"FANHIGH")==0) {
        _holdDownTimer = 100;
        relayControl("FANHIGH");
    }
    if (strcmp(data,"FANLOW")==0) {
        _holdDownTimer = 100;
        relayControl("FANLOW");
    }
    if (strcmp(data,"PUMP")==0) {
        _holdDownTimer = 100;
        relayControl("PUMP");
    }
    if (strcmp(data,"OFF")==0) {
        _holdDownTimer = 100;
        relayControl("OFF");
    }
    if (strcmp(data,"SMART")==0) {
        _holdDownTimer = 0;
    }
    
}

void gotWeatherData(const char *name, const char *data) {
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
    String str = String(data);
    char strBuffer[125] = "";
    str.toCharArray(strBuffer, 125);
    lastGotWeather = Time.now();
    fcast_h = atof(strBuffer);
}


// RD relay controls
int relayControl(String command)
    {
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
            _pump_on = 1;
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
    _holdDownTimer = 10;
    return 1;
    }


void loop()
{

    // Decrement holdDownTimer
    if (_holdDownTimer>0){
        _holdDownTimer--;
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
                    pubFlow("Failed to read from DHT sensor 5 times!");
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
	if ( (humidity_upstairs / humidity_upstairs_avg) > (1-humidityVariance) && (humidity_upstairs / humidity_upstairs_avg) < (1+humidityVariance)){
	    bool valSetUpstairsHumidity = thingspeak.recordValue(4, String(humidity_upstairs).substring(0,4));
	}else{
        pubFlow("humidity_upstairs outside mean: " + String(humidity_upstairs) + ". mean: " + String(humidity_upstairs_avg));
	}

	if ( (humidity_downstairs / humidity_downstairs_avg) > (1-humidityVariance) && (humidity_downstairs / humidity_downstairs_avg) < (1+humidityVariance) ){
	    humidity_downstairs_hist[0] = humidity_downstairs;
	    bool valSetDownstairsHumidity = thingspeak.recordValue(1, String(humidity_downstairs).substring(0,4));
	}else{
        pubFlow("humidity_downstairs outside mean: " + String(humidity_downstairs) + ". mean: " + String(humidity_downstairs_avg));
	}

	if ( (temp_upstairs / temp_upstairs_avg) > (1-tempVariance) && (temp_upstairs / temp_upstairs_avg) < (1+tempVariance)){
	    temp_upstairs_hist[0] = temp_upstairs;
	    bool valSetUpstairsTemp = thingspeak.recordValue(5, String(temp_upstairs_hist[0]).substring(0,4));
	}else{
        pubFlow("temp_upstairs outside mean: " + String(temp_upstairs) + ". mean: " + String(temp_upstairs_avg));
	}

	if ( (temp_downstairs / temp_downstairs_avg) > (1-tempVariance) && (temp_downstairs / temp_downstairs_avg) < (1+tempVariance)){
	    temp_downstairs_hist[0] = temp_downstairs;
	    bool valSetDownstairsTemp = thingspeak.recordValue(2, String(temp_downstairs).substring(0,4));
	}else{
        pubFlow("temp_downstairs outside mean: " + String(temp_downstairs) + ". mean: " + String(temp_downstairs_avg));
	}

    // Set latest Thingspeak non-sensor values
    bool valSetPump = thingspeak.recordValue(7, String(_holdDownTimer));
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
        pubFlow("Writing to thingspeak failed =(: " + String(valsSent));
		return;
	}
    
    // If the temp drops below the comfortable temperature, turn the bloody thing off!    
    
    if ( (temp_upstairs_hist[0] < _comfyTemp || temp_downstairs_hist[0] < _comfyTemp ) && _fan_speed && _holdDownTimer == 0) {
        relayControl("PUMP");
        pubFlow("Brr, Utmp is:" + String(temp_upstairs_hist[0]).substring(0,4) + "(" + String(temp_upstairs_avg).substring(0,4) + ") Dtmp is:" + String(temp_downstairs_hist[0]).substring(0,4) + "(" + String(temp_downstairs_avg).substring(0,4) + ")"  );
    } else {
        if (temp_upstairs_hist[0] > _comfyTemp && _holdDownTimer == 0) {
        // If the temp surpasses _comfyTemp, and the forecast is hot (>80), cool it down!   
            if (temp_upstairs_hist[0] - _comfyTemp > 5){
                if ( _fan_speed  < 2 ){
                    relayControl("COOLHIGH");
                }
            }else{
                if ( _fan_speed != 1 ){
                    relayControl("COOLLOW");
                } else {
                    pubFlow("Temp<5, FANONOFF:" + String(digitalRead(FANONOFF)) + " FANHIGHLOW:" + String(digitalRead(FANHIGHLOW)) );
                }
            }
        } else {
            pubFlow("No change to cooler operation.");
        }
    }
    // Wait until the next minute before running the loop again.
    while(Time.second() != 0){
        delay(10);
    }
    delay(1100);
    
}
