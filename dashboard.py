from flask import Flask, render_template, jsonify
import random
import time

app = Flask(__name__)

# Configurazione delle stanze (simulazione database o config file)
ROOMS = [
    {"id": "living", "name": "Soggiorno"},
    {"id": "kitchen", "name": "Cucina"},
    {"id": "bedroom", "name": "Camera da Letto"},
    {"id": "office",  "name": "Ufficio"} # Ho aggiunto una 4a stanza per mostrare la scalabilità
]

@app.route("/")
def index():
    # Passiamo la lista delle stanze al template HTML per generare le card dinamicamente
    return render_template("index.html", rooms=ROOMS)

@app.route("/api/data")
def data():
    # Generiamo dati diversi per ogni stanza
    response_data = {}
    
    for room in ROOMS:
        # Simulazione leggermente diversa per ogni stanza per realismo
        base_temp = 20 if room['id'] == 'bedroom' else 24
        base_hum = 50
        
        # Fluttuazione casuale
        current_temp = base_temp + random.uniform(-2, 5) # Es: tra 18 e 29 gradi
        current_hum = base_hum + random.uniform(-10, 20)
        
        response_data[room['id']] = {
            "temperature": round(current_temp, 1),
            "humidity": int(current_hum),
            "timestamp": time.strftime("%H:%M:%S")
        }
        
    return jsonify(response_data)

if __name__ == "__main__":
    app.run(debug=True, port=5000)