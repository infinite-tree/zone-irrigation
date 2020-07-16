// Global Status
var RUNNING = false;
var HOURS = 0;


// Extend buttons to be enabled & disabled
jQuery.fn.extend({
    disable: function(state) {
        return this.each(function() {
            var $this = $(this);
            $this.toggleClass('disabled', state);
        });
    }
});


//
// Update Progress bar & Button
//
function updateStartButton() {
    if (RUNNING) {
        $("#start-btn").text("STOP");
        $("#start-btn").disable(false);
        $("#start-btn").removeClass("btn-primary").addClass("btn-danger");
    } else {
        $("#start-btn").text("Start");
        $("#start-btn").disable(true);
        $("#start-btn").removeClass("btn-danger").addClass("btn-primary");
    }
}

function updateProgress() {
    // if (!RUNNING) {
    //     console.log("NOT RUNNING");
    //     return;
    // }
    // console.log("Getting status...");
    
    $.getJSON("status", function( data ) {
        console.log("Data: ");
        console.log(data);
        var message = data["message"],
            percent = data["percent"];
    
        if (data["status"] === "RUNNING") {
            RUNNING = true;

            $("#status-txt").text(message);
            $("#progress-bar").removeClass("progress-bar-striped progress-bar-animated")
            $("#progress-bar").css("width", percent+"%").attr('aria-valuenow', percent);


        } else if (data["status"] === "OFF") {
            $("#status-txt").text("OFF");
            $("#progress-bar").removeClass("progress-bar-striped progress-bar-animated")
            $("#progress-bar").css("width", "0%").attr('aria-valuenow', 0);
            RUNNING = false;
        } else {
            // Text status
            $("#status-txt").text(message);
            $("#progress-bar").addClass("progress-bar-striped progress-bar-animated")
            $("#progress-bar").css("width", "100%").attr('aria-valuenow', 100);
            RUNNING = true;

        }
        updateStartButton();
    });

    setTimeout(updateProgress, 30000);
}

//
// Handle Start button click
//
function start() {
    var data = new FormData();

    // alert("Value is " + HOURS);
    if (RUNNING) {
        $.ajax({
            url: "/stop",
            type: 'POST',
            data: data,
            cache: false,
            dataType: 'json',
            processData: false,
            contentType: false,
    
            success: function(response) {
                updateStartButton();
                // progress bar update will handle the rest

            },
            error: function(jqXHR, textStatus, errorThrown){
                alert("Failed to STOP");
            }
        });
    
    } else {
        data.append("hours", HOURS);
        $.ajax({
            url: "/start",
            type: 'POST',
            data: data,
            cache: false,
            dataType: 'json',
            processData: false,
            contentType: false,

            success: function(response) {
                RUNNING = true;
                $("#start-btn").disable(true);
                // setTimeout(updateStartButton, 1000);

                // Text status
                $("#status-txt").text("Initializing");

                // progress bar initial state
                $("#progress-bar").addClass("progress-bar-striped progress-bar-animated")
                $("#progress-bar").css("width", "100%").attr('aria-valuenow', 0);

                updateProgress();

            },
            error: function(jqXHR, textStatus, errorThrown){
                alert("Failed to Start");
            }
        });

    }
}


$(document).ready(function() {
    //
    // Handle hours selection
    //
    $(".btn-group-md > button.btn").on("click", function(){
        HOURS = +this.innerHTML;
        $("#start-btn").disable(false);
        // alert("Value is " + HOURS);
    });

    //
    // Register Start button handler
    //
    $("#start-btn").click(start);
    updateProgress();

});