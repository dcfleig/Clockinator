/* eslint-disable  func-names */
/* eslint quote-props: ["error", "consistent"]*/
/**
 * This sample demonstrates a simple skill built with the Amazon Alexa Skills
 * nodejs skill development kit.
 **/

'use strict';
const Alexa = require('alexa-sdk');

let thingColor = "Red";

// 1. Text strings =====================================================================================================
//    Modify these strings and messages to change the behavior of your Lambda function

const config = {};
config.IOT_BROKER_ENDPOINT      = "a1p7fx1rx0lfgm.iot.us-east-1.amazonaws.com";  // also called the REST API endpoint
config.IOT_BROKER_REGION        = "us-east-1";  // eu-west-1 corresponds to the Ireland Region.  Use us-east-1 for the N. Virginia region
config.IOT_THING_NAME_POSTFIX   = "Clockinator"

//=========================================================================================================================================
//TODO: The items below this comment need your attention.
//=========================================================================================================================================

//Replace with your app ID (OPTIONAL).  You can find this value at the top of your skill's page on http://developer.amazon.com.
//Make sure to enclose your value in quotes, like this: const APP_ID = 'amzn1.ask.skill.bb4045e6-b3e8-4133-b650-72923c5980f1';
const APP_ID = 'amzn1.ask.skill.05095b0c-f95f-4565-ad12-b6caa8715e99';

const SKILL_NAME = 'Red Clockinator';
const WELCOME_MESSAGE = 'Tell me what you would like to show on the red clockinator.';
const HELP_MESSAGE = 'You can say, show me the time and outside temperature. I can show the time, date, outside temperature and the outside feel on either the left or right side.  You can also say exit...What would you like me to do?';
const HELP_REPROMPT = 'What do you want me to show on the red clockinator?';
const STOP_MESSAGE = 'Goodbye!';


// 2. Skill Code =======================================================================================================

const handlers = {
    'LaunchRequest': function () {
        console.log("in LaunchRequest");
        this.response.speak(WELCOME_MESSAGE).listen(HELP_REPROMPT);
        this.emit(':responseReady');
        console.log("leaving LaunchRequest");
    },
    'DisplayIntent': function () {
        console.log("in DisplayIntent");

        var speechOutput;
        //delegate to Alexa to collect all the required slot values
        var filledSlots = delegateSlotCollection.call(this);

        var color="Red";
        var leftSource=this.event.request.intent.slots.LeftSourceSlot.value;
        var leftSourceId=this.event.request.intent.slots.LeftSourceSlot.resolutions.resolutionsPerAuthority[0].values[0].value.id;

        //rightSource is optional so we'll add it to the output
        //only when we have a valid activity
        var rightSourceId;
        var rightSource = isSlotValid(this.event.request, "RightSourceSlot");
        if (rightSource) {
            rightSourceId=this.event.request.intent.slots.RightSourceSlot.resolutions.resolutionsPerAuthority[0].values[0].value.id;
        } 

        // Need to capitalize the first color of the color
        thingColor = color.charAt(0).toUpperCase() + color.substr(1);

        if (leftSource == "*nothing") leftSource = "";
        if ((rightSource == null) || (rightSource == "+nothing")) rightSource = "";

        var desiredContentJSON;

        if (rightSource) {
            speechOutput = "Displaying " + leftSource + " and " +rightSource+ " on the " + color + " clockinator";  
            desiredContentJSON = {"leftSource":leftSourceId,"rightSource":rightSourceId} ;
        } else {
            speechOutput = "Displaying " + leftSource + " on the " + color + " clockinator"        
            desiredContentJSON = {"leftSource":leftSourceId} ;
        }

        console.log(speechOutput);
        console.log("desired value json: " + desiredContentJSON);

        console.log("sending update shadow message");
        updateShadow(thingColor,desiredContentJSON, status => {
            this.response.speak(speechOutput);
            this.emit(':responseReady');
        });             
        console.log("leaving DisplayIntent");
    },
    'AMAZON.HelpIntent': function () {
        const speechOutput = HELP_MESSAGE;
        const reprompt = HELP_REPROMPT;

        this.response.speak(speechOutput).listen(reprompt);
        this.emit(':responseReady');
    },
    'AMAZON.CancelIntent': function () {
        this.response.speak(STOP_MESSAGE);
        this.emit(':responseReady');
    },
    'AMAZON.StopIntent': function () {
        this.response.speak(STOP_MESSAGE);
        this.emit(':responseReady');
    },
};

exports.handler = function (event, context, callback) {
    const alexa = Alexa.handler(event, context, callback);
    alexa.appId = APP_ID;
    alexa.registerHandlers(handlers);
    alexa.execute();
};

//    END of Intent Handlers {} ========================================================================================
// 3. Helper Function  =================================================================================================


function updateShadow(thingColor, desiredState, callback) {
    var thingName = thingColor + config.IOT_THING_NAME_POSTFIX;
    console.log("in updateShadow for thing " + thingName);
    // update AWS IOT thing shadow
    var AWS = require('aws-sdk');
    AWS.config.region = config.IOT_BROKER_REGION;

    //Prepare the parameters of the update call

    var paramsUpdate = {
        "thingName" : thingName,
        "payload" : JSON.stringify(
            { "state":
                { "desired": desiredState             
                }
            }
        )
    };
    var iotData = new AWS.IotData({endpoint: config.IOT_BROKER_ENDPOINT});

    iotData.updateThingShadow(paramsUpdate, function(err, data)  {
        if (err){
            console.log("Error updating shadow for " + thingName)
            console.log(err);

            callback("not ok");
        }
        else {
            console.log("updated thing shadow " + thingName + ' to state ' + paramsUpdate.payload);
            callback("ok");
        }

    });
}

function delegateSlotCollection(){
  console.log("in delegateSlotCollection");
  console.log("current dialogState: "+this.event.request.dialogState);
    if (this.event.request.dialogState === "STARTED") {
      console.log("in Beginning");
      var updatedIntent=this.event.request.intent;
      //optionally pre-fill slots: update the intent object with slot values for which
      //you have defaults, then return Dialog.Delegate with this updated intent
      // in the updatedIntent property
      this.emit(":delegate", updatedIntent);
    } else if (this.event.request.dialogState !== "COMPLETED") {
      console.log("in not completed");
      // return a Dialog.Delegate directive with no updatedIntent property.
      this.emit(":delegate");
    } else {
      console.log("in completed");
      console.log("returning: "+ JSON.stringify(this.event.request.intent));
      // Dialog is now complete and all required slots should be filled,
      // so call your normal intent handler.
      return this.event.request.intent;
    }
}

function isSlotValid(request, slotName){
        var slot = request.intent.slots[slotName];
        //console.log("request = "+JSON.stringify(request)); //uncomment if you want to see the request
        var slotValue;

        //if we have a slot, get the text and store it into speechOutput
        if (slot && slot.value) {
            //we have a value in the slot
            slotValue = slot.value.toLowerCase();
            return slotValue;
        } else {
            //we didn't get a value in the slot.
            return false;
        }
}