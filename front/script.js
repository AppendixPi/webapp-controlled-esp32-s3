const canvas = document.getElementById('sphereCanvas');
const ctx = canvas.getContext('2d');
const sq_dim = 250;		//square dimension
const radius_dim = 150; //radius dimension

let pressTimer; // Variable to track long press duration
let isOn = false;

const colorPickerBottom = document.getElementById('colorPickerBottom');
const colorPickerCenter = document.getElementById('colorCenter');

/*
* Add websocket 
*/
var gateway =  `ws://${window.location.hostname}/ws`;
var websocket;
window.addEventListener('load', onLoad);
function initWebSocket() {
    console.log('Trying to open a WebSocket connection...');
    websocket = new WebSocket(gateway);
    websocket.onopen = onOpen;
    websocket.onclose = onClose;
	websocket.onerror = onError;
    websocket.onmessage = onMessage; //This event acts as a client's ear to the server. Whenever the server sends data, the onmessage event gets fired
}

function onOpen(event) {
	console.log('Connection opened');
}
function onClose(event) {
	console.log('Connection closed');
	setTimeout(initWebSocket, 2000);
}
function onMessage(event) {
	console.log(event.data);
}
function onLoad(event) {
	initWebSocket();
}

function onError(event) {
	console.log('WS Error!');
}


function sendColorData() {
  if (!websocket || websocket.readyState !== WebSocket.OPEN) {
    console.error("WebSocket is not connected or not open.");
    return;
  }

  // Assuming colorPickerBottom.value contains a string like "#RRGGBB"
  const colorPickerValue = colorPickerBottom.value; // Example: "#FF5733"
  
  var r = 0; // Red
  var g = 0; // Green
  var b = 0; // Blue
  
  console.log("Poke state:", isOn);
  if(isOn === true){	//If false, turn off the LEDs
	// Extract RGB values from the hex color
	 r = parseInt(colorPickerValue.slice(1, 3), 16); // Red
	 g = parseInt(colorPickerValue.slice(3, 5), 16); // Green
	 b = parseInt(colorPickerValue.slice(5, 7), 16); // Blue
  }



  // Create the JSON structure
  const jsonData = {
    pixel: [
      {
        id: -1,
        R: r,
        G: g,
        B: b
      }
    ]
  };

  // Convert the JSON object to a string
  const jsonString = JSON.stringify(jsonData);

  // Send the JSON string over the WebSocket
  websocket.send(jsonString);

  console.log("Data sent over WebSocket:", jsonString);
}


// Default color
let sphereColor = colorPickerBottom.value;

// Event listener for color picker change
colorPickerBottom.addEventListener('input', (event) => {
    sphereColor = event.target.value; // Update the sphere color
    drawSphere(); // Redraw the sphere with the new color
});

function drawSphereSlice(radius, startAngle, endAngle) {
    ctx.beginPath();
    ctx.arc(sq_dim, sq_dim, radius, startAngle, endAngle, false);
    ctx.lineTo(sq_dim + radius * Math.cos(endAngle), sq_dim + radius * Math.sin(endAngle)); // Draw line to the edge
    ctx.closePath();
    ctx.fillStyle = isOn ? 'rgba(255, 0, 0, 0.6)' : 'white'; // Updated color;
    ctx.fill();
}

function drawBand(radius, startAngle, endAngle) {
    ctx.beginPath();
	if(startAngle > 0){
		ctx.arc(sq_dim, sq_dim, radius, 0, startAngle, false);
		ctx.lineTo(sq_dim + radius * Math.cos(endAngle), sq_dim + radius * Math.sin(endAngle)); // Draw line to the edge
		ctx.arc(sq_dim, sq_dim, radius, endAngle, Math.PI, false);
		ctx.lineTo(sq_dim + radius , sq_dim  ); // Draw line to the edge
	}else{
		ctx.arc(sq_dim, sq_dim, radius, startAngle, 0, false);
		ctx.lineTo(sq_dim + radius , sq_dim  ); // Draw line to the edge
		
		ctx.arc(sq_dim, sq_dim, radius, Math.PI,endAngle, false);
		ctx.lineTo(sq_dim + radius * Math.cos(endAngle), sq_dim + radius * Math.sin(endAngle)); // Draw line to the edge
	}

    ctx.closePath();
    ctx.fillStyle ='black'; // Updated color;
    ctx.fill();
}


// Function to draw the sphere
function drawSphere() {
    ctx.clearRect(0, 0, canvas.width, canvas.height);

    // Set shadow properties
    ctx.shadowColor = 'rgba(0, 0, 0, 0.5)'; // Shadow color
    ctx.shadowBlur = 15; // Blur level of the shadow
    ctx.shadowOffsetX = 5; // Horizontal offset of the shadow
    ctx.shadowOffsetY = 5; // Vertical offset of the shadow

    // Draw top half with new color
    ctx.beginPath();
    ctx.arc(sq_dim, sq_dim, radius_dim, Math.PI, 0, false); // Top half
    ctx.lineTo(sq_dim, sq_dim); // Back to center
    ctx.closePath();
    ctx.fillStyle = 'rgba(0, 140, 149, 0.5)'; // Updated color
    ctx.fill();

    // Draw bottom half with new color
	ctx.beginPath();
    ctx.arc(sq_dim, sq_dim, radius_dim, 0, Math.PI, false); // Bottom half
    ctx.lineTo(sq_dim, sq_dim); // Back to center
    ctx.closePath();
    ctx.fillStyle = isOn ? sphereColor : 'white'; // Updated color
    ctx.fill();
	
	// Draw central black band
	drawBand(radius_dim, Math.PI/18, Math.PI - Math.PI/18 );     // Bottom slice
	drawBand(radius_dim, -Math.PI/18, Math.PI + Math.PI/18  );     // Bottom slice

    // Draw black circle in the middle
    ctx.beginPath();
    ctx.arc(sq_dim, sq_dim, sq_dim/5.25, 0, Math.PI * 2); // Black circle
    ctx.fillStyle = 'black';
    ctx.fill();

    // Draw white circle in the middle of the black circle
    ctx.beginPath();
    ctx.arc(sq_dim, sq_dim, sq_dim/10, 0, Math.PI * 2); // White circle
    ctx.fillStyle = 'white';
    ctx.fill();
	
	sendColorData();
	
}

// Event listener for canvas click
canvas.addEventListener('click', (event) => {
    const { offsetX, offsetY } = event;
    const bottomHalfY = sq_dim + sq_dim/10;//sq_dim; // Center Y position of the canvas

    // Check if the click is on the bottom half
    if ((offsetY > bottomHalfY)&(offsetY < sq_dim+radius_dim)) {
        isOn = !isOn; // Toggle color
        drawSphere(); // Redraw the sphere
    }
});

// Detect long press on canvas for both mouse and touch
canvas.addEventListener('mousedown', startPressTimer);
canvas.addEventListener('mouseup', clearPressTimer);
canvas.addEventListener('mouseleave', clearPressTimer);
canvas.addEventListener('touchstart', startPressTimer);
canvas.addEventListener('touchend', clearPressTimer);
canvas.addEventListener('touchcancel', clearPressTimer);

function startPressTimer(event) {
    //event.preventDefault(); // Prevent default behavior for touch events
	colorPickerBottom.style.display = 'none'; // Hide color picker after selection
	
	const offsetXPosition = 50; // Offset in pixels to the right
    const offsetYPosition = 50; // Offset in pixels downwards

    const { offsetX, offsetY } = event.type === 'touchstart' 
        ? { offsetX: event.touches[0].clientX - canvas.getBoundingClientRect().left,
            offsetY: event.touches[0].clientY - canvas.getBoundingClientRect().top }
        : event;

    const bottomHalfY = sq_dim + sq_dim / 10;

    // Check if the click is in the bottom half region
    if (offsetY > bottomHalfY && offsetY < sq_dim + radius_dim) {
        pressTimer = setTimeout(() => {
			const xPosition = (event.pageX || event.touches[0].pageX) - offsetXPosition;
            const yPosition = (event.pageY || event.touches[0].pageY) - offsetYPosition;
            colorPickerBottom.style.left = `${xPosition}px`;
            colorPickerBottom.style.top = `${yPosition}px`;
            colorPickerBottom.style.display = 'block';
			isOn = !isOn; // Toggle color
            //colorPickerBottom.click();
        }, 500);
    }
}

function clearPressTimer() {
    clearTimeout(pressTimer);
}

// Initial draw
drawSphere();
