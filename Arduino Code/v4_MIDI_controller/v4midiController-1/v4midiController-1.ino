/*
 * Autor: Franco Grassano - YAELTEX
 * ---
 * INFORMACIÓN DE LICENCIA
 * Kilo Mux Shield por Yaeltex se distribuye bajo una licencia
 * Creative Commons Atribución-CompartirIgual 4.0 Internacional - http://creativecommons.org/licenses/by-sa/4.0/
 * ----
 * Código para el manejo de los integrados 74HC595 tomado de http://bildr.org/2011/02/74HC595/
 * Librería de multiplexado (modificada) tomada de http://mayhewlabs.com/products/mux-shield-2
 * Librería para el manejo del sensor de ultrasonido tomada de http://playground.arduino.cc/Code/NewPing
 * 
 * Este código fue desarrollado para el KILO MUX SHIELD desarrolado en conjunto por Yaeltex y el Laboratorio del Juguete, en Buenos Aires, Argentina, 
 * apuntando al desarrollo de controladores MIDI con arduino.
 * Está preparado para manejar 2 (expandible) registros de desplazamiento 74HC595 conectados en cadena (16 salidas digitales en total),
 * y 2 multiplexores CD4067 de 16 canales cada uno (16 entradas analógicas, y 16 entradas digitales), pero es expandible en el
 * caso de utilizar hardware diferente. Para ello se modifican los "define" NUM_MUX, NUM_CANALES_MUX y NUM_595s. 
 * NOTA: Se modificó la librería MuxShield, para trabajar sólo con 1 o 2 multiplexores. Si se necesita usar más multiplexores, descargar la librería original.
 * 
 * Para las entradas analógicas, por cuestiones de ruido se recomienda usar potenciómetros o sensores con buena estabilidad, y con preferencia con valores
 * cercanos o menores a 10 Kohm.
 * Agradecimientos:
 * - Jorge Crowe
 * - Lucas Leal
 * - Dimitri Diakopoulos
 */

/*
 * Inclusión de librerías. 
 */

#include <KM_Data.h>
#include <Kilomux.h>
#include <MIDI.h>

void setup(); // Esto es para solucionar el bug que tiene Arduino al usar los #ifdef del preprocesador

#define MIDI_COMMS

#if defined(MIDI_COMMS)
struct MySettings : public midi::DefaultSettings
{
    static const unsigned SysExMaxSize = 256; // Accept SysEx messages up to 1024 bytes long.
};
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Create a 'MIDI' object using MySettings bound to Serial2.
MIDI_CREATE_CUSTOM_INSTANCE(HardwareSerial, Serial, MIDI, MySettings);
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
#endif

#define BLINK_INTERVAL 200

static const char * const modeLabels[] = {
    "Off"
  , "Note"
  , "CC"
  , "NRPN"
  , "Shifter"
};
#define MODE_LABEL(m)   ((m) <= KMS::M_SHIFTER ? modeLabels[m] : "???")

// AJUSTABLE - Si hay ruido que varía entre valores (+- 1, +- 2, +- 3...) colocar el umbral en (1, 2, 3...)
#define NOISE_THRESHOLD             1                      // Ventana de ruido para las entradas analógicas. Si entrada < entrada+umbral o 
#define NOISE_THRESHOLD_NRPN        3                      // Ventana de ruido para las entradas analógicas. Si entrada < entrada+umbral o 

#define ANALOG_UP     1
#define ANALOG_DOWN   0

KMS::GlobalData gD = KMS::globalData();

Kilomux KMShield;                                       // Objeto de la clase Kilomux
bool digitalInputState[NUM_MUX*NUM_MUX_CHANNELS];   // Estado de los botones

// Contadores, flags //////////////////////////////////////////////////////////////////////
byte i, j, mux, channel;                       // Contadores para recorrer los multiplexores
byte numBanks, numInputs, numOutputs;
bool ledMode;
bool flagBlink = 0, configMode = 0, receivingSysEx = 0;                         
unsigned long millisPrevLED = 0;               // Variable para guardar ms
static byte blinkCount = 0;
unsigned int packetSize;
///////////////////////////////////////////////////////////////////////////////////////////

void setup() {
  #if defined(MIDI_COMMS)
  MIDI.begin(MIDI_CHANNEL_OMNI); // Se inicializa la comunicación MIDI.
  MIDI.turnThruOff();            // Por default, la librería de Arduino MIDI tiene el THRU en ON, y NO QUEREMOS ESO!
  #else
  Serial.begin(115200);                  // Se inicializa la comunicación serial.
  #endif

  // Initialize Kilomux shield
  KMShield.init();
  // Initialize EEPROM handling library
  KMS::initialize();

  pinMode(LED_BUILTIN, OUTPUT);
  Serial.print(gD.protocolVersion());
  if(gD.protocolVersion() == 1) 
    packetSize = 57;
    
  ResetConfig();
}

void loop(){
 
  #if defined(MIDI_COMMS)
  if(MIDI.read())
    ReadMidi();
  #endif

  blinkLED();

  if(!receivingSysEx) ReadInputs();

}

void ResetConfig(void){
  if(gD.isValid()){
    ledMode = gD.hasOutputMatrix();
    numBanks = gD.numBanks();
    numInputs = gD.numInputsNorm();
    numOutputs = gD.numOutputs();
    #if !defined(MIDI_COMMS)
    Serial.print("Numero de bancos: "); Serial.println(numBanks);
    Serial.print("Numero de entradas: "); Serial.println(numInputs);
    Serial.print("Numero de salidas: "); Serial.println(numOutputs);
    #endif
  }
  else{
    #if !defined(MIDI_COMMS)
    Serial.print("Datos en EEPROM no válidos");
    #endif
    while(1);
  }

  KMS::setBank(0); 
  configMode = 0;  
  
  // Setear todos las lecturas a 0
  for (mux = 0; mux < NUM_MUX; mux++) {                
    for (channel = 0; channel < NUM_MUX_CHANNELS; channel++) {
      KMShield.muxReadings[mux][channel] = KMShield.analogReadKm(mux, channel);
      KMShield.muxPrevReadings[mux][channel] = KMShield.muxReadings[mux][channel];
      digitalInputState[channel+mux*NUM_MUX_CHANNELS] = 0;
    }
  }
}

void blinkLED(){
  static bool lastLEDState = LOW;
  static unsigned long millisPrev = 0;
  static bool firstTime = true;
  
  if(flagBlink && blinkCount){
    if (firstTime){ firstTime = false; millisPrev = millis(); }
    if(millis()-millisPrev > BLINK_INTERVAL){
      millisPrev = millis();
      digitalWrite(13, !lastLEDState);
      lastLEDState = !lastLEDState;
      if(lastLEDState == LOW) blinkCount--;
      if(!blinkCount){ flagBlink = 0; firstTime = true; }
    }
  }
  return;
}

#if defined(MIDI_COMMS)
// Lee el canal midi, note y velocity, y actualiza el estado de los leds.
void ReadMidi(void) {
  switch(MIDI.getType()){
    byte data1;
    byte data2;
    case midi::NoteOn:
      data1 = MIDI.getData1();
      data2 = MIDI.getData2();
      if(data2 > 64){
         KMShield.digitalWriteKm(data1, HIGH);
      }
      else{
         KMShield.digitalWriteKm(data1, LOW);
      }
  break;
  case midi::NoteOff:
    data1 = MIDI.getData1();
    data2 = MIDI.getData2();

    if(!data2) KMShield.digitalWriteKm(data1, LOW);
  break;
  case midi::SystemExclusive:
    int sysexLength = 0;
    const byte *pMsg;
      
    sysexLength = (int) MIDI.getSysExArrayLength();
    pMsg = MIDI.getSysExArray();

    char sysexID[3]; 
    sysexID[0] = (char) pMsg[1]; 
    sysexID[1] = (char) pMsg[2]; 
    sysexID[2] = (char) pMsg[3];
      
    char command = pMsg[4];

    if(sysexID[0] == 'Y' && sysexID[1] == 'T' && sysexID[2] == 'X'){
      configMode = 1;
      
      if (command == 1){            // Enter config mode
        flagBlink = 1;
        blinkCount = 1;
        const byte configAckSysExMsg[5] = {'Y', 'T', 'X', 2, 0};
        MIDI.sendSysEx(5, configAckSysExMsg, false);
      }                             
       
      if(command == 3 && configMode){             // Save dump data
        if (!receivingSysEx){
          receivingSysEx = 1;
          MIDI.sendNoteOn(0, receivingSysEx,1);
        }
        
        KMS::io.write(packetSize*pMsg[5], pMsg+6, sysexLength-7);      // PACKET_SIZE is 200, pMsg has index in byte 6, total sysex packet has max. 207 bytes 
                                                                        //                                                |F0, Y , T , X, command, index, F7|
        MIDI.sendNoteOn(127, pMsg[5],1);                                                                          
        flagBlink = 1;                                              
        blinkCount = 1;
        
        if (sysexLength < packetSize+7){   // Last message?
          receivingSysEx = 0;
          MIDI.sendNoteOn(0, receivingSysEx,1);
          flagBlink = 1;
          blinkCount = 3;
          const byte dumpOkMsg[5] = {'Y', 'T', 'X', 4, 0};
          MIDI.sendSysEx(5, dumpOkMsg, false);
          ResetConfig();
        }  
      }  
    }
    break;
  } 
}
#endif

/*
 * Esta función lee todas las entradas análógicas y/o digitales y almacena los valores de cada una en la matriz 'lecturas'.
 * Compara con los valores previos, almacenados en 'lecturasPrev', y si cambian, y llama a las funciones que envían datos.
 */
void ReadInputs() {
  for(int contadorInput = 0; contadorInput < numInputs; contadorInput++){
    KMS::InputNorm input = KMS::input(contadorInput);
    mux = contadorInput/NUM_MUX_CHANNELS;
    channel = contadorInput%NUM_MUX_CHANNELS;
   // Serial.println(contadorInput);
    if (input.mode() != KMS::M_OFF){
      if(input.analog() == KMS::T_ANALOG){
        if(input.mode() == KMS::M_NRPN)
          KMShield.muxReadings[mux][channel] = KMShield.analogReadKm(mux, channel);          // Si es NRPN leer entradas analógicas 'KMShield.analogReadKm(N_MUX,N_CANAL)'
        else                                                                                 
          KMShield.muxReadings[mux][channel] = KMShield.analogReadKm(mux, channel) >> 3;     // Si no es NRPN, leer entradas analógicas 'KMShield.analogReadKm(N_MUX,N_CANAL)'
                                                                                         // El valor leido va de 0-1023. Convertimos a 0-127, dividiendo por 8.
                                                                                         
        if (!IsNoise(KMShield.muxReadings[mux][channel], KMShield.muxPrevReadings[mux][channel], 
                                           contadorInput, input.mode() == KMS::M_NRPN ? true : false)) {  // Si lo que leo no es ruido
          // Enviar mensaje. Si es NRPN, multiplicar por 16 el valor para cubrir la resolución completa (16.384)                                                                                  
          InputChanged(contadorInput, input, KMShield.muxReadings[mux][channel]);    
        }
        else{
          continue;                                                                          // Sigo con la próxima lectura
        }
        KMShield.muxPrevReadings[mux][channel] = KMShield.muxReadings[mux][channel];             // Almacenar lectura actual como anterior, para el próximo ciclo
      }
      
      else if(input.analog() == KMS::T_DIGITAL){
        // CÓDIGO PARA LECTURA DE ENTRADAS DIGITALES Y SHIFTERS
        KMShield.muxReadings[mux][channel] = KMShield.digitalReadKm(mux, channel, PULLUP);      // Leer entradas digitales 'KMShield.digitalReadKm(N_MUX, N_CANAL)'
        
        //Serial.print("Mux: "); Serial.print(mux); Serial.print("  Channel: "); Serial.println(channel);
        if (KMShield.muxReadings[mux][channel] != KMShield.muxPrevReadings[mux][channel]) {     // Me interesa la lectura, si cambió el estado del botón,
          
          KMShield.muxPrevReadings[mux][channel] = KMShield.muxReadings[mux][channel];             // Almacenar lectura actual como anterior, para el próximo ciclo
          if (!KMShield.muxReadings[mux][channel]){
            digitalInputState[channel+mux*NUM_MUX_CHANNELS] = !digitalInputState[channel+mux*NUM_MUX_CHANNELS];   // MODO TOGGLE: Cambia de 0 a 1, o viceversa
                                                                                                                  // MODO NORMAL: Cambia de 0 a 1                                                                                                                  
            InputChanged(contadorInput, input, KMShield.muxReadings[mux][channel]);
          }
          else if (KMShield.muxReadings[mux][channel]) {
            digitalInputState[channel+mux*NUM_MUX_CHANNELS] = 0;
            InputChanged(contadorInput, input, KMShield.muxReadings[mux][channel]);
          }
          
        }
      }
    }
//      if(entradaActivada[mux][canal]){
//        if(tipoEntrada[mux][canal] == ANALOGICA){
//          // ENTRADAS ANALÓGICAS 1 ///////////////////////////////////////////////////
//          unsigned int analogData = KMShield.analogReadKm(mux, canal);          // Leer entradas analógicas 'KMShield.analogReadKm(N_MUX,N_CANAL)'
//          lecturas[mux][canal] = analogData >> 3;                                 // El valor leido va de 0-1023. Convertimos a 0-127, dividiendo por 8.
//  
//          if (!EsRuido(lecturas[mux][canal], lecturasPrev[mux][canal], canal, mux)) {                                              // Si lo que leo no es ruido
//            #if defined(COMUNICACION_MIDI)|defined(HAIRLESS_MIDI)
//              EnviarControlChangeMidi(lecturas[mux][canal], canal);                                     // Envío datos MIDI
//            #elif defined(COMUNICACION_SERIAL)
//              EnviarControlChangeSerial(lecturas[mux][canal], canal);                                   // Envío datos SERIAL
//            #endif
//          }
//          else{
//            continue;                                                          // Sigo con la próxima lectura
//          }
//        ///////////////////////////////////////////////////////////////////////////
//          lecturasPrev[mux][canal] = lecturas[mux][canal];             // Almacenar lectura actual como anterior, para el próximo ciclo  
//        }
//        // ENTRADAS DIGITALES /////////////////////////////////////////////////////
//        else if(tipoEntrada[mux][canal] == DIGITAL){
//          // CÓDIGO PARA LECTURA DE ENTRADAS DIGITALES Y SHIFTERS
//          lecturas[mux][canal] = KMShield.digitalReadKm(mux, canal, PULLUP);      // Leer entradas digitales 'KMShield.digitalReadKm(N_MUX, N_CANAL)'
//  
//          if (lecturas[mux][canal] != lecturasPrev[mux][canal]) {             // Me interesa la lectura, si cambió el estado del botón,
//            lecturasPrev[mux][canal] = lecturas[mux][canal];                  // Almacenar lectura actual como anterior, para el próximo ciclo
//            
//            if (!lecturas[mux][canal]){                                               // Si leo 0 (botón accionado)                                                                                                                                                        
//              #if defined(SHIFTERS)                                                   // Si estoy usando SHIFTERS                                                                                        
//              if (byte shift = EsShifter(canal)){
//                CambiarBanco(shift);
//                continue;   // Si es un shifter, seguir con la próxima lectura.
//              }
//              #endif    // endif SHIFTERS
//              
//              digitalInputState[bancoActivo][canal] = !digitalInputState[bancoActivo][canal];     // MODO TOGGLE: Cambia de 0 a 1, o viceversa
//                                                                                      // MODO NORMAL: Cambia de 0 a 1
//              // Se envía el estado del botón por MIDI o SERIAL
//              #if defined(COMUNICACION_MIDI)|defined(HAIRLESS_MIDI)
//              EnviarNoteMidi(canal, digitalInputState[bancoActivo][canal]*127);                                  // Envío MIDI
//              #elif defined(COMUNICACION_SERIAL)
//              EnviarNoteSerial(canal, digitalInputState[bancoActivo][canal]*127);                                // Envío SERIAL
//              estadoLeds[bancoActivo][mapeoLeds[mapeoNotes[canal]]] = digitalInputState[bancoActivo][canal]*2;   // Esta línea cambia el estado de los LEDs cuando se presionan los botones (solo SERIAL)
//              cambioEstadoLeds = 1;                                                                        // Actualizar LEDs
//              #endif              // endif COMUNICACION
//            }   
//                                                      
//            else if (lecturas[mux][canal]) {                        // Si se lee que el botón pasa de activo a inactivo (lectura -> 5V) y el estado previo era Activo
//              #if !defined(TOGGLE)
//              digitalInputState[bancoActivo][canal] = 0;                  // Se actualiza el flag a inactivo
//              #endif
//              
//              #if defined(SHIFTERS)                                 // Si estoy usando SHIFTERS      
//              // Si el botón presionado es uno de los SHIFTERS de bancos
//                if (EsShifter(canal)){
//                  #if !defined(TOGGLE_SHIFTERS)
//                  CambiarBanco(-1);  
//                  #endif
//                  continue;
//                }           
//              #endif       // endif SHIFTERS
//              #if !defined(TOGGLE)                                          // Si no estoy usando el modo TOGGLE
//                #if defined(COMUNICACION_MIDI)|defined(HAIRLESS_MIDI)    
//                EnviarNoteMidi(canal, NOTE_OFF);                      // Envío MIDI
//                #elif defined(COMUNICACION_SERIAL)              
//                EnviarNoteSerial(canal, NOTE_OFF);                    // Envío SERIAL
//                estadoLeds[bancoActivo][mapeoLeds[mapeoNotes[canal]]] = LED_APAGADO;
//                cambioEstadoLeds = 1;
//                #endif      // endif COMUNICACION                                      
//              #endif     // endif TOGGLE
//            } 
//          }
//        }
//      }
  }
}


void InputChanged(int numInput, const KMS::InputNorm &input, unsigned int value) {
  byte currBank = KMS::bank();
  byte mode = input.mode();
  bool analog = input.analog();
  byte param = input.param_fine() + numInputs*currBank;
  byte channel = input.channel() + 1;
  byte velocity = !value * NOTE_ON;
  
  #if defined(MIDI_COMMS)
  if(configMode){
    if(analog){
      MIDI.sendControlChange( numInput, value, 1);
    }
    else{
      MIDI.sendNoteOn( numInput, velocity, 1); 
    }
  }
  else{
    switch(mode){
      case (KMS::M_NOTE):
        MIDI.sendNoteOn( param, velocity, channel);     // Channel is 1-16 to Arduino MIDI Lib, that's why + 1 is there
      break;
      case (KMS::M_CC):
        MIDI.sendControlChange( param, value, channel);
      break;
      case KMS::M_NRPN:
        MIDI.sendControlChange( 101, input.param_coarse(), channel);
        MIDI.sendControlChange( 100, input.param_fine(), channel);
        MIDI.sendControlChange( 6, (value >> 7) & 0x7F, channel);
        MIDI.sendControlChange( 38, (value & 0x7F), channel);
      break;
      case KMS::M_PROGRAM:
        if(value > 0){
          MIDI.sendProgramChange( param, channel);
        }
      break;
      case KMS::M_SHIFTER:
        
      break;
      default:
      break;
    }
  }
  #else
    if(mode == KMS::M_NOTE){
      value = !value * NOTE_ON;
    }
    else if(mode == KMS::M_NRPN){
      value = map(value, 0, 1023, 0, 16383);
    }
    Serial.print("Channel: "); Serial.print(channel); Serial.print("\t");
    Serial.print("Tipo: "); Serial.print(input.analog()?"Analog":"Digital"); Serial.print("\t");
    Serial.print("Modo: "); Serial.print(MODE_LABEL(mode)); Serial.print("\t");
    Serial.print("Parameter: "); Serial.print((input.param_coarse() << 7) | input.param_fine()); Serial.print("  Valor: "); Serial.println(value);
  #endif
  
  return; 
}

/*
 * Funcion para filtrar el ruido analógico de los pontenciómetros. Analiza si el valor crece o decrece, y en el caso de un cambio de dirección, 
 * decide si es ruido o no, si hubo un cambio superior al valor anterior más el umbral de ruido.
 * 
 * Recibe: - 
 */

unsigned int IsNoise(unsigned int value, unsigned int prevValue, unsigned int input, bool nrpn) {
  static bool upOrDown[NUM_MUX_CHANNELS*2] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
                                            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  byte noiseTh;
  
  if (nrpn) noiseTh = NOISE_THRESHOLD_NRPN;
  else      noiseTh = NOISE_THRESHOLD;
                                            
  if (upOrDown[input] == ANALOG_UP){
    if(value > prevValue){              // Si el valor está creciendo, y la nueva lectura es mayor a la anterior, 
       return 0;                        // no es ruido.
    }
    else if(value < prevValue - noiseTh){   // Si el valor está creciendo, y la nueva lectura menor a la anterior menos el UMBRAL
      upOrDown[input] = ANALOG_DOWN;                                     // se cambia el estado a DECRECIENDO y 
      return 0;                                                               // no es ruido.
    }
  }
  if (upOrDown[input] == ANALOG_DOWN){                                   
    if(value < prevValue){  // Si el valor está decreciendo, y la nueva lectura es menor a la anterior,  
       return 0;                                        // no es ruido.
    }
    else if(value > prevValue + noiseTh){    // Si el valor está decreciendo, y la nueva lectura mayor a la anterior mas el UMBRAL
      upOrDown[input] = ANALOG_UP;                                       // se cambia el estado a CRECIENDO y 
      return 0;                                                               // no es ruido.
    }
  }
  return 1;         // Si todo lo anterior no se cumple, es ruido.
}