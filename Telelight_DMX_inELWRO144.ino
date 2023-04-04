/* 
Nazwa programu: Telelight DMX controller
Autor: Telepator - Marcin Szczepański
Data utworzenia: 2018
Ostatnia modyfikacja: 2019
Wersja: 1.00
Opis programu: jest to sterownik oświetlenia scenicznego w standardzie DMX512
Wymagania systemowe: Program pracuje na mikrokontrolerach wraz z dodatkowymi układami przedstawionymi na załączonych schamatach.
Do uruchomienia programu potrzebny jest również kompatybilny programator mikrokontrolera oraz środowisko programistyczne do jego programowania, takie jak np. Arduino IDE.
*/

#include <SPI.h>               // biblioteka SD potrzebuje SPI do komunikacji
#include <SD.h>                // zapis , odczyt z kart SD
#include <Keypad.h>            // część odpowiedzialna za klawiaturę ELWRO 144
#include <DmxSimple.h>         // biblioteka odpowiedzialna za wysyłanie sygnalów dmx. ten projekt posiada także układ MAX 485  symetryzujacy sygnał
#include <LiquidCrystal.h>     // wyświetlacz alfanumeryczny lcd 
#include <TimerOne.h>          // obsługa wątków     

const byte ROWS = 3; // po rozrysowaniu płytki (podkreślmy, że jednowarstwowej!) z klawiatury matrycowej ELWRO144 układ ma się następująco
const byte COLS = 10;
//define the cymbols on the buttons of the keypads
char hexaKeys[ROWS][COLS] = {
  { 'C','E','=','F','x','H','<','x','B','6' },
  { '8','D','?','>','9','@',':','3','2','4' },
  { 'x','7',';','5','G','x','A','x','x','1' }
};
byte rowPins[ROWS] = { 42, 44, 46 }; //connect to the row pinouts of the keypad
byte colPins[COLS] = { 22, 24, 26, 28, 30, 32, 34, 36, 38, 40 }; //connect to the column pinouts of the keypad
Keypad elwro144 = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);  //initialize an instance of class NewKeypad
// KONIEC część odpowiedzialna za klawiaturę ELWRO 144

// initialize the library with the numbers of the interface pins
LiquidCrystal lcd(12, 11, 7, 6, 5, 4);

byte okok[8] = {                                                       // pięć nowo zdefiniowanych znaków
  B01001,
  B01010,
  B01100,
  B01010,
  B11101,
  B10100,
  B10100,
  B11100
};
byte minusik[8] = {
  B00000,
  B01110,
  B00000,
  B00001,
  B10010,
  B10100,
  B11000,
  B11110
};
byte plusik[8] = {
  B00100,
  B01110,
  B00100,
  B01111,
  B00011,
  B00101,
  B01001,
  B10000
};
byte blackoff[8] = {
  B11111,
  B10001,
  B10001,
  B10001,
  B10001,
  B10001,
  B10001,
  B11111
};
byte blackon[8] = {
  B10101,
  B01110,
  B11111,
  B01110,
  B11111,
  B01110,
  B11111,
  B01010
};

int wyjsciowe[129];    //   index 0 nie uzywamy DMX zaczyna się od 1 kanału. Tablica wyjsciowe to jakby stan na wysciu DMX, jakie wartosci aktualne posiada zewnetrzne urzadzenie
int docelowe[129];     //   z presetów będą kopiowane wartości do tablicy docelowe oraz do tablicy channelmode. Wartosci z wyjsciowych (aktualnych) dąża do wartosci docelowych 
int channelmode[129];  //   0 = natychmiast bez glideRGB czyli pętla steps k= 0 , 1 = glideRGB - płynne przejscie, 2 = po przejsciu pętli steps gdy k=255
int channelknob[129];  //   tutaj zaznaczamy te kanaly , które odpowiadają za jasność świecenia
byte channelblackout[129];  // grupowanie urzadzen do 4 funkcji blackout
byte blackout;
bool blackoutKeyB[4];
byte blackoutKey[4];
static int tempp; 
static int *temp = &tempp;
int skann = 0;            // skanowanie klawiatury
static int *skan = &skann;
int klawiszz = 0;         // który klawisz
static int *klawisz = &klawiszz;
int menustan[10];  // indeks = 0- tryb urzadzenia  0- wyłączone wysyłanie, 1-włączone wysyłanie, 2 ,  4-kalkulator kanałów DMX
                   // indeks = 1 - stan wyswietlacza, pierwsze okno, pierwszy poziom.Tryby: preset, edit, stuff, sequencer edit, kalc,0 - tryb zapisu do pamięci 
                   // indeks = 2 - który bank 
                   // indeks = 3 - który preset/klawisz wybrany z klawiatury, jaki numer 1-12
                   // indeks = 4 - który preset ma być wczytany (1-12)+12*nrbanku (klawisz plus numer banku razy 12)
                   // indeks = 5 - edytowany kanał lub grupa 1-7 8-14 itd   ch124v000 m1 DIM
                   // indeks = 6 - ilosc urzadzen, w edycji poszczegolnych urzadzeń, pozostałe przyciemniają lub gaszą się DIM= mozna edytować w ustawieniach ogólnych.   

File plik;
int binarne[9];  // channel calc
volatile bool flaga;    // sterowanie programem, użycie wątków
volatile bool flaga2;
volatile bool flaga3;
volatile bool blokada[8];  // blokada potencjometrów, funkcja "łapania zmiennych"
int starypreset=0;


  
void setup() {
  // Serial.begin(57600);//initialize Serial connection
  pinMode(53, OUTPUT);
  if (!SD.begin(53)) {    // bez tego biblioteka SD nie działa...
    //Serial.println("initialization failed!");
    while (1);
  }
  //  Serial.println("initialization done.");
   
  lcd.begin(16, 2);
  lcd.createChar(256, okok);
  lcd.createChar(257, minusik);
  lcd.createChar(258, plusik);
  lcd.createChar(259, blackoff);
  lcd.createChar(260, blackon);
  
  wyswietl(0, 0, "ELWRO 144 DMX v8");
  delay(400);
  wyswietl(0, 1, "PRACA DYPLOMOWA");
  
  for (int j = 0; j < 129; j++) {  // inicjalizacja zmiennych. 129-ilosc kanałów
    wyjsciowe[j] = 0;
    docelowe[j] = 0;
    channelmode[j] = 0;
    channelknob[j] = 0;
    channelblackout[j] = 0;
  }
  *(menustan + 1) = 1;
  *(menustan + 2) = 0;
  *(menustan + 3) = 1;
  *(menustan + 4) = 1;
  *(menustan + 5) = 1;
  flaga = 0 ;
  flaga2 = 1;
  blackout = 0;
  
  Timer1.initialize(65000);
  Timer1.attachInterrupt(skanKlawiatury); 

}


void loop()
{  
  interrupts();
  menu();
  delay(10);
}



void skanKlawiatury()
{
  *skan = elwro144.getKey();   // odczyt z klawiatury ELWRO 144 
  if (*skan != NO_KEY)
  {
     blackout=0; 
     *klawisz = *skan - 48;    // zamiana realnej wartości znaków ascii, aby numery klawiszy zaczynaly sie od 1 do 24
     if (*klawisz <7)          // TRYB PRACY STEROWNIKA: 1 Presety - Pamięć , 2 Kanały manualnie-edycja, 3 Gałki manualnie - edycja 4. Kalkulator DMX, 5. Zapis do pamięci
     {
        *(menustan + 1) = *klawisz;   // tryb pracy przechowywany w tablicy menustan[1] 
        lcd.clear();                  // czysczenie wyswietlacza po kazdej zmianie menu  
     }
     if (*(menustan + 1) == 1 )   
     { 
        *(menustan + 2) = plusmin(*(menustan + 2));    // ktory bank
        presetklawisz();                               //ustalenie  menustan+ 3, czyli preset z klawiatury(klawisze 13-24) 
       
        if( starypreset != *(menustan + 3))
        {
          if (flaga2 == 1 )                            // jeżeli przejsciewnowy() się nie zakończyło, to nalezy wymusic zakończenie
          flaga3=0;                                    // kończy pętlę w przejciewnowy() ustawia flagę
          starypreset = *(menustan + 3);
        }
        *(menustan + 4) = *(menustan + 3) + (*(menustan + 2) * 12);   // obliczenie, ktory preset wczytac
        blackoutSkan();                                               //  funkcja blackout
     }
     flaga = 1; 
        
  }
}



void menu() {
  if(flaga==1)
  {
    wyswietlacz();
    if (*(menustan + 1) == 1 )                     // menu - Preset mode   ||||||||||||||||||||||||||
    {      
      if (*klawisz >= 13){
        flaga3=1;
      if (*(menustan + 1) == 1 && flaga2==0)
        wczytaj(*(menustan + 4));
        przejscieWNowy();
      }
      if (*klawisz>8 && *klawisz<13 && flaga2==0)          // gaszenie lamp - funkcja blackout oraz dimmer knobe
      {
        for (int i=0; i<15; i++)   
        {
          float dimmer=(analogRead(A0))/1023.0;           // odczyt potencjometru dimmer
          byte konbitKey;                                 // wynik koniunkcji bitowej 
          konbitKey = blackout & channelblackout[i];
          if (konbitKey==0 && channelknob[i]==1){
            DmxSimple.write(i, (int)*(wyjsciowe+i)*dimmer); // jezeli kanal jest oznaczony jako dimmer, to mozna posrednio zmienic jego wartosc
          }else if(konbitKey==0 && channelknob[i]!=1){
            DmxSimple.write(i, *(wyjsciowe+i));
          }else if(konbitKey>0){                           // jeżeli był wspólny bit, to ustaw wartosc na 0, urzadzenie wylaczone
            DmxSimple.write(i, 0);  
          }
        }
      }
    }
    
    if (*(menustan + 1) == 2)                      // menu - Chennel edit - tryb kanałów, zmiana kanałów i ich edycja     ||||||||||||||||||||
    {      //  zmiana banków
      *(menustan + 5) = plusmin(*(menustan + 5));
      *(menustan + 5) = plusmin10(*(menustan + 5));
      *(menustan + 5) = plusmin100(*(menustan + 5));
      wyswietltrzy(0, 1, *(menustan + 5));     // wyświetlanie numeru edytowanego kanału

      *(wyjsciowe + (*(menustan + 5))) = plusmin111(*(wyjsciowe + (*(menustan + 5))));   // edycja wartosci kanalu - przyciski z funkcji plusmin111()
      DmxSimple.write(*(menustan + 5), *(wyjsciowe + (*(menustan + 5))));                // wysłanie wartosci do zew. urzadzenia, aby uzytkownik widzial efekt swoich zmian
      wyswietltrzy(4, 1, (*(wyjsciowe + (*(menustan + 5)))));                            // wyswietla wartosc edytowanego kanalu

      wyswietl(8, 1, "G");     //  fragment dla edycji channelmode zwiazanej z glide - czy wartosci maja przechodzic natychmiast czy płynnie czy po pętli kroków
      *(channelmode + (*(menustan + 5))) = plusG(*(channelmode + (*(menustan + 5))));
      wyswietl(9, 1, *(channelmode + (*(menustan + 5))));

      wyswietl(11, 1, "D");    //  fragment dla edycji channelmode zwiazanej z parametrem dimmer, czyli który kanał odpowiada za jasność świecienia
      *(channelknob + (*(menustan + 5))) = plusD(*(channelknob + (*(menustan + 5))));
      wyswietl(12, 1, *(channelknob + (*(menustan + 5))));

      wyswietl(14, 1, "B");    //  fragment dla edycji channelblackout zwiazanej z funkcja blackout, czyli możliwoscia natychmiastowgo wylaczenia poprzez dimmer calych sekcji lamp 
      if (*(channelknob + (*(menustan + 5)))!=0)
      {
        *(channelblackout + (*(menustan + 5))) = plusB(*(channelblackout + (*(menustan + 5))));
        if(*(channelblackout + (*(menustan + 5)))==8)    // zamiana 8 (B00001000) na wartosci mile oku od 0 do 4 zamiast 0, 1, 2, 4, 8
          wyswietl(15, 1, 4);
        else if(*(channelblackout + (*(menustan + 5)))==4)
          wyswietl(15, 1, 3);
        else 
          wyswietl(15, 1, *(channelblackout + (*(menustan + 5))));
      }else
      {
        *(channelblackout + (*(menustan + 5)))=0;
        wyswietl(15, 1, *(channelblackout + (*(menustan + 5))));
      }
      
    }

    if (*(menustan + 1) == 3)                           // menu - Knobe mode  ||||||||||||||||||||||||||||||
    {
      *(menustan + 5) = plusmin(*(menustan + 5));
      *(menustan + 5) = plusmin7(*(menustan + 5));
      for (int i=0; i<8; i++)
      blokada[i]=0;
      wyswietltrzy(0, 1, *(menustan + 5));     // wyświetlanie numeru edytowanego kanału
    }
    
    if (*(menustan + 1) == 4)                           // menu - DMX Addr CalC ||||||||||||||||||||||||||||
    {
      *(menustan + 5) = plusmin(*(menustan + 5));
      wyswietltrzy(1, 1, *(menustan + 5));     // wyświetlanie numeru edytowanego kanału
      kanalbinarnie(*(menustan + 5));
      int i=7;
      for(int j=0; j<=8 ; j++){
      wyswietl(i, 1, binarne[j]);
      i++;
      }
    }

  wyswietlacz();
  flaga = 0;
  }             // powyzszy kod wykonuje sie tylko po nacisnieciu ktoregos z klawiszy       
  
    if (*(menustan + 1) == 3)                           // menu - Knobe mode, praca ciagła
    {
      int value[7];
      value[0] = analogRead(A6);     // odczyt z potencjometrów
      value[1] = analogRead(A5);
      value[2] = analogRead(A4);
      value[3] = analogRead(A3);
      value[4] = analogRead(A2);
      value[5] = analogRead(A1);
      value[6] = analogRead(A0);
      // Przeskalowanie wartości potencjometrów do wartości DMX 0-255
      int roznica[7]; 
      int knobe[7];
      for (int i=0; i<7; i++)
      {
         knobe[i] = map(value[i], 0, 1023, 0, 255);   // problem sprzętowy... brak zasilania referencyjnego, układ nie zawsze pokaze maksymalna wartosc, czyli 5V
         roznica[i]=*(wyjsciowe + (*(menustan + 5)+i))-knobe[i];
         if(roznica[i]>0)     
          lcd.setCursor((9+i), 1),lcd.write(258); 
         else
          lcd.setCursor((9+i), 1),lcd.write(257);
                  
         if(*(wyjsciowe + (*(menustan + 5)+i))==knobe[i]){
          blokada[i]=1; 
          lcd.setCursor((9+i), 1);
          lcd.write(256);
         }
         if(blokada[i]==1)
          *(wyjsciowe + (*(menustan + 5)+i))=knobe[i];
         DmxSimple.write((*(menustan + 5)+i), *(wyjsciowe + (*(menustan + 5)+i)));      
      }
      wyswietltrzy(4,1,*(wyjsciowe + (*(menustan + 5))));  // wyswoetlanie wartosci edytowanego kanału
    } 

    // menu: zapis
    if (*klawisz == 5 || menustan[1] == 5)              // menu - zapis
    {
      menustan[1] = 5;
      if (*klawisz > 12)
      {
        presetklawisz();                   //ustalenie  menustan+ 3
        *(menustan + 4) = *(menustan + 3) + (*(menustan + 2) * 12);
        zapisz(*(menustan + 4));
        menustan[1] = 1;                   // przejscie do pierwszego menu, czyli preset mode
        lcd.clear();
        wyswietlacz();
      }
    }    // koniec menu - zapis


}  



void przejscieWNowy()
{
  flaga2=1;
  for (int j=0; j<256; j++ )                       // pętla kroków steps - przejście jednego koloru w drugi odbywa się w 256 krokach    
  {     
     delay(speedRGB());                           
     wyswietltrzy(13,0,map(j,0,255,255,0));
     for (int i=0; i<15; i++)    //512 129 zmiana na 15 . Maksymalna ilosc kanalow jest niemozliwa. Program mocno zwalnia
     {   
        float dimmer=(analogRead(A0))/1023.0; 
        byte konbitKey;
        konbitKey = blackout & channelblackout[i];
        if (*(channelmode+i)==0 )      // natychmiastowe przejscie wartosci kanalu  - G==0
        {     
           *(wyjsciowe+i)=*(docelowe+i);
            
             if (konbitKey==0 && channelknob[i]==1){
                DmxSimple.write(i, (int)*(wyjsciowe+i)*dimmer);
             }else if(konbitKey==0 && channelknob[i]!=1){
                DmxSimple.write(i, *(wyjsciowe+i));
             }else if(konbitKey>0){   
                DmxSimple.write(i, 0);  
             }
          continue;
        }
        else if (*(channelmode+i)==1)        // tryb z płynnym przejsciem kanału G==1
        {     
          *temp = glideRGB(*(wyjsciowe+i), *(docelowe+i), j);
          *(wyjsciowe+i) = *temp;
             if (konbitKey==0 && channelknob[i]==1){
                DmxSimple.write(i, *temp);
             }else if(konbitKey==0 && channelknob[i]!=1){
                DmxSimple.write(i, *temp);
             }else if(konbitKey>0){   
                DmxSimple.write(i, 0);  
             }
          //DmxSimple.write(i, *temp); 
          continue;  
        }else if (*(channelmode+i)==2 && j==255)        // G==2   przejscie na koniec pętli
        {
        *(wyjsciowe+i)=*(docelowe+i);
             if (konbitKey==0 && channelknob[i]==1){
                DmxSimple.write(i, (int)*(wyjsciowe+i)*dimmer);
             }else if(konbitKey==0 && channelknob[i]!=1){
                DmxSimple.write(i, *(wyjsciowe+i));
             }else if(konbitKey>0){   
                DmxSimple.write(i, 0);  
             }          
          //DmxSimple.write(i, *(wyjsciowe+i));
         continue;
        }
      }
    if(*(menustan + 1) != 1  || flaga3==0){  
      //Serial.print("Break w petli");  
      break;    
    }

  }
  flaga2 = 0;

}


String nazwaPresetu(int numerPreset) {    // ok
  String nazwaa = ".txt";
  String numer = String(numerPreset);
  numer.concat(nazwaa);
  return numer;
}

void zapisz(int _numerek)   // zapisywanie presetów (tablic) na karcie SD
{

  if (SD.exists(nazwaPresetu(_numerek)))
  {
    SD.remove(nazwaPresetu(_numerek));
  }
  delay(200);
  plik = SD.open(nazwaPresetu(_numerek), FILE_WRITE);     // nazwa pliku jest numerem presetu
  if (plik)
  {
    for (int i = 0; i < 129; i++)   // zapis kolejnych tablic do pliku
    {
      plik.println(wyjsciowe[i]);
    }
    for (int i = 0; i < 129; i++)
    {
      plik.println(channelmode[i]);
    }
    for (int i = 0; i < 129; i++)
    {
      plik.println(channelknob[i]);
    }
    for (int i = 0; i < 129; i++)
    {
      plik.println(channelblackout[i]);
    }

  }
  else
  {
    wyswietl(0, 1, "blad zapisu");
    delay(2000);
  }
  plik.close();
  lcd.clear();
  wyswietl(0, 0, "**** saved ****");
  delay(1000);

}



void wczytaj(int _numerek)       //menustan{4}  indeks = 4 - który preset ma być wczytany (1-12)+12*nrbanku
{
  int liczn = 0;
  int *licz = &liczn;
  String wartosc = "0";
  int asciiwartosc = 0;

  plik = SD.open(nazwaPresetu(_numerek));
  if (plik)
  {
    *licz = 0;
    int asciiwartosc = 0;
    while (plik.available())
    {
      asciiwartosc = plik.read();
      //Serial.print(asciiwartosc);
      //Serial.print("-");
      if (asciiwartosc == 10)
      {
        *licz = *licz + 1;
        wartosc = "0";
      }
      if (asciiwartosc > 47)
      {
        asciiwartosc = asciiwartosc - 48;
        wartosc.concat(asciiwartosc);
        if (*licz < 129)
          *(docelowe + (*licz)) = wartosc.toInt();
        else if (*licz > 128 && *licz < 258)
        {
          *(channelmode + ((*licz) % 129)) = wartosc.toInt();
        }
        else if (*licz > 257 && *licz < 388)
        {
          *(channelknob + ((*licz) % 129)) = wartosc.toInt();
        }
        else if (*licz > 387 && *licz < 518)
        {
          *(channelblackout + ((*licz) % 129)) = wartosc.toInt();
        }
      }
    }
    plik.close();
   // Serial.print("odczyt ok");
  }
  else
  {
    wyswietl(0, 0, "Error...");
  }

}


int unsigned speedRGB() {
  int speedGlide2 = analogRead(A7);         // ODCZYT WARTOŚCI Z POTENCJONOMETRU SPEED
  speedGlide2 = map(speedGlide2, 0, 1023, 80, 0);       // ZMIANA ZAKRESU 0 - 80 JEST WYSTARCZAJĄCE DLA TRYBU SPEED
  return speedGlide2;
}


// funkcja glideRGB odpowiada za płynne przejścia kolorów(wartości poszczególnych zmiennych), jej czas można regulować w pętli steps
int glideRGB(int &wyjsciowa, int &docelowa, int &steps) {
  float etap = (steps) / 255.0;             // rozdzielczość przejscia - kroki            
  int wynik = wyjsciowa + (int)(docelowa - wyjsciowa)*etap;
  return wynik;
}


// funkcje odpowiedzialne za edycje zmiennych z klawiatury
void presetklawisz() {
  if (*klawisz >= 13)
  {
    *(menustan + 3) = *klawisz - 12;
   }
}



int plusmin(int unsigned _wartosc) {
  if (*klawisz == 7 && _wartosc > 0 && *(menustan+1)==1)
    _wartosc = _wartosc - 1;
  else if (*klawisz == 7 && _wartosc > 1)
    _wartosc = _wartosc - 1;  
  if (*klawisz == 8 && _wartosc < 511)
    _wartosc = _wartosc + 1;
  return _wartosc;
}

int plusmin10(int unsigned _wartosc) {
  if (*klawisz == 9 && _wartosc > 10)
    _wartosc = _wartosc - 10;
  if (*klawisz == 10 && _wartosc<502)
    _wartosc = _wartosc + 10;
  return _wartosc;
}
int plusmin100(int unsigned _wartosc) {
  if (*klawisz == 11 && _wartosc > 100)
    _wartosc = _wartosc - 100;
  if (*klawisz == 12 && _wartosc<412)
    _wartosc = _wartosc + 100;
  return _wartosc;
}
int plusmin7(int unsigned _wartosc) {
  if (*klawisz == 9 && _wartosc > 7)
    _wartosc = _wartosc - 7;
  if (*klawisz == 10 && _wartosc < 505)
    _wartosc = _wartosc + 7;
  return _wartosc;
}
int plusmin111(int unsigned _wartosc) {
  if (*klawisz == 15 && _wartosc < 255)
    _wartosc = _wartosc + 1;
  if (*klawisz == 21 && _wartosc > 0)
    _wartosc = _wartosc - 1;

  if (*klawisz == 14 && _wartosc < 246)
    _wartosc = _wartosc + 10;
  if (*klawisz == 20 && _wartosc > 9)
    _wartosc = _wartosc - 10;

  if (*klawisz == 13 && _wartosc < 156)
    _wartosc = _wartosc + 100;
  if (*klawisz == 19 && _wartosc > 99)
    _wartosc = _wartosc - 100;
  return _wartosc;
}
int plusmin512(int unsigned _wartosc) {
  if (*klawisz == 15 && _wartosc < 511)
    _wartosc = _wartosc + 1;
  if (*klawisz == 21 && _wartosc > 0)
    _wartosc = _wartosc - 1;

  if (*klawisz == 14 && _wartosc < 502)
    _wartosc = _wartosc + 10;
  if (*klawisz == 20 && _wartosc > 9)
    _wartosc = _wartosc - 10;

  if (*klawisz == 13 && _wartosc < 412)
    _wartosc = _wartosc + 100;
  if (*klawisz == 19 && _wartosc > 99)
    _wartosc = _wartosc - 100;
  return _wartosc;
}
int plusG(int unsigned _wartosc) {
  if (*klawisz == 16 && _wartosc < 2)
    _wartosc = _wartosc + 1;
  if (*klawisz == 22 && _wartosc > 0)
    _wartosc = _wartosc - 1;
  return _wartosc;
}
int plusD(int unsigned _wartosc) {
  if (*klawisz == 17 && _wartosc < 1)
    _wartosc = _wartosc + 1;
  if (*klawisz == 23 && _wartosc > 0)
    _wartosc = _wartosc - 1;
  return _wartosc;
}
int plusB(byte _wartosc) {
  if (*klawisz == 18 && _wartosc ==0 )
    _wartosc = 1;
  else if (*klawisz == 18 && _wartosc < 8 )
    _wartosc = _wartosc << 1;
  if (*klawisz == 24 && _wartosc > 0)
    _wartosc = _wartosc >> 1;
  return _wartosc;
}


void blackoutSkan(){
  for (int j=0;j<4; j++){
    if(*klawisz==(9+j) && blackoutKeyB[j]==0){
         blackoutKey[j]=blackoutKey[j] | (B00000001 << j);
     blackoutKeyB[j]=1;
    }
    else if (*klawisz==(9+j) && blackoutKeyB[j]==1){
      blackoutKey[j] = B00000000;
    blackoutKeyB[j]=0;
    }
  }
  for (int k=0; k<4; k++){
      blackout = blackout | blackoutKey[k];
     } 

}




void wyswietl(int _pozycja, int _linia, int _tresc)
{
  lcd.setCursor(_pozycja, _linia);
  lcd.print(_tresc);
}

void wyswietl(int _pozycja, int _linia, String _tresc)
{
  lcd.setCursor(_pozycja, _linia);
  lcd.print(_tresc);
}
// wyswietla wartosci 2 liczbowe
void wyswietldwa(int _pozycja, int _linia, int _wartosc)
{
  if (_wartosc < 10) {
    lcd.setCursor(_pozycja, _linia);
    lcd.print("_");
    lcd.setCursor((_pozycja + 1), _linia);
    lcd.print(_wartosc);
  }
  else if (_wartosc >= 10) {
    lcd.setCursor(_pozycja, _linia);
    lcd.print(_wartosc);
  }
}
// wyswietla wartosci 3 liczbowe
void wyswietltrzy(int _pozycja, int _linia, int _wartosc)
{
  if (_wartosc < 10) {
    lcd.setCursor(_pozycja, _linia);
    lcd.print("  ");
    lcd.setCursor((_pozycja + 2), _linia);
    lcd.print(_wartosc);
  }
  else if (_wartosc >= 10 && _wartosc < 100) {
    lcd.setCursor(_pozycja, _linia);
    lcd.print(" ");
    lcd.setCursor((_pozycja + 1), _linia);
    lcd.print(_wartosc);
  }
  else if (_wartosc >= 100) {
    lcd.setCursor(_pozycja, _linia);
    lcd.print(_wartosc);
  }
}

void kanalbinarnie(int _licz)
{
  for (int i = 0; i<9;i++){
     binarne[i]=_licz%2;
    _licz/=2;    
    }
}



void wyswietlacz() {             //  wyświetlanie statycznych elementów menu niewymagających odświeżania

  switch (menustan[1]) {
  case 0:
    wyswietl(0, 0, "Save? press");
    wyswietl(0, 1, "Y-preset N-mode");
    break;
  case 1:
    wyswietl (0 , 0 , "Preset mode");
    wyswietl(0, 1, "B");
    if (*(menustan + 2) < 10) {
      wyswietl(1, 1, "_");
      wyswietl(2, 1, *(menustan + 2));     // numer banku
    }
    else if (*(menustan + 2) >= 10) {
      wyswietl(1, 1, *(menustan + 2));
    }
                                           // nr presetu
    wyswietl(4, 1, "P");
    if (*(menustan + 3) < 10) {
      wyswietl(5, 1, "_");
      wyswietl(6, 1, *(menustan + 3));
    }
    else if (*(menustan + 3) >= 10) {
      wyswietl(5, 1, *(menustan + 3));
    }
    wyswietltrzy(13, 1, *(menustan + 4));    // nr presetu niezaleznie od banku, czyli nasz plik txt w przypadku kart sd jako pamięci
    for (int i=0; i<4; i++){                 // wyswietlanie informacji o włączonych lub wyłączonych sekcjach blackout
      if(blackoutKeyB[i]==1){
        lcd.setCursor((9+i), 1);
        lcd.write(259);
      }else if(blackoutKeyB[i]==0){
        lcd.setCursor((9+i), 1);
        lcd.write(260);
      }
    }
    break;
  case 2:
    wyswietl(0, 0, "Channel edit");
    break;
  case 3:
    wyswietl(0, 0, "Knobe mode");
    break;
  case 4:
    wyswietl(0, 0, "DMX Addr Calc");
    wyswietl(0, 1, "A");
    wyswietl(5, 1, "Sw");
    break;    
  case 5:
    wyswietl(0, 0, "Save? press");
    wyswietl(0, 1, "Y-preset N-mode");
    break; 
  }
}
