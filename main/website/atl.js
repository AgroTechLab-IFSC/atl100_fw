const form = document.querySelector('form');
function uploadFile(event) {
    /** @type {HTMLFormElement} */
    const form = event.currentTarget;
    const url = new URL(form.action);
    const formData = new FormData(form);
    const searchParams = new URLSearchParams(formData);
  
    /** @type {Parameters<fetch>[1]} */
    const fetchOptions = {
      method: form.method,
    };
  
    if (form.method.toLowerCase() === 'post') {
      if (form.enctype === 'multipart/form-data') {
        fetchOptions.body = formData;
      } else {
        fetchOptions.body = searchParams;
      }
    } else {
      url.search = searchParams;
    }
  
    fetch(url, fetchOptions);
  
    event.preventDefault();
}

function delayRedirect(){
    document.getElementById('delayMsg').innerHTML = 'Please wait while the ATL-100 is restarting! You\'ll be redirected after <span id="countDown">15</span> seconds...';
    var count = 15;
    setInterval(function(){
        count--;
        document.getElementById('countDown').innerHTML = count;
        if (count == 2) {
            window.location = window.location.href; 
        }
    },1000);
}

function getConfJSONFile(){
    var xhr = new XMLHttpRequest();
    xhr.open("GET", "/api/v1/system/get/conf", true);
    xhr.responseType = 'json';
    xhr.onreadystatechange = function() {
        if (xhr.readyState === 4 && xhr.status === 200) {
            var jsonResponse = xhr.response;            
            const link = document.createElement("a");            
            const file = new Blob([JSON.stringify(jsonResponse, null, 2)], { type: 'application/json' });
            link.href = URL.createObjectURL(file);
            link.download = "atl100_config.json";
            link.click();
            URL.revokeObjectURL(link.href);
        }
    };
    xhr.send();
}