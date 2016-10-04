typedef struct __attribute__((__packed__))
{
    int timezone;
    uint8_t brightness;
    uint16_t sync_interval;
    bool mil_time;
    char ntp_server[128];
} clock_config_t;

clock_config_t clock_config;

void reset_clock_config(){
    NL();
    OL("Resetting config...");
    memset(&clock_config, 0, sizeof(clock_config_t));
    clock_config.timezone = 0;
    clock_config.brightness = 50;
    clock_config.sync_interval = 60; //minutes
    clock_config.mil_time = true;
    String ntp_server = String("us.pool.ntp.org");
    OL(ntp_server);
    memcpy(clock_config.ntp_server, ntp_server.c_str(), sizeof(char) * ntp_server.length());
    OL(clock_config.ntp_server);
}

void write_clock_config(){
    OL("Before");
    OL(clock_config.ntp_server);
    EWA(1, clock_config);
    OL("After");
    OL(clock_config.ntp_server);
}

#define CLOCK_CONFIG_CHECK 12
void read_clock_config(){
    if(EEPROM.read(0) != CLOCK_CONFIG_CHECK){
        reset_clock_config();
        write_clock_config();
        EEPROM.write(0, CLOCK_CONFIG_CHECK);
        EEPROM.commit();
    }

    ERA(1, clock_config);

    OL(clock_config.ntp_server);
    OL("TEST");
}

void __flush_serial(){
    while(Serial.available() > 0){
        Serial.read();
    }
}

String _prompt(String prompt, char mask = ' ', int timeout = 0);

String _prompt(String prompt, char mask, int timeout){
    static char cmd[PROMPT_INPUT_SIZE];
    static int count;
    static char tmp;
    memset(cmd, 0, PROMPT_INPUT_SIZE);
    count = 0;
    if(timeout > 0){
        NL(); OF("Timeout in "); O(timeout); OFL("s...");
    }

    int start = millis();

    O(prompt.c_str());
    OF("> ");

    while(true){
        if(Serial.available() > 0){
            tmp = Serial.read();
            if(tmp != '\n' && tmp != '\r'){
                cmd[count] = tmp;
                if(mask==' ')
                    Serial.write(tmp);
                else
                    Serial.write(mask);
                Serial.flush();
                count++;
            }
            else{
                __flush_serial();
                NL(); NL();
                return String(cmd);
            }
        }
        delay(1);
        if(timeout > 0 && (millis()-start) > (timeout * 1000)){
            return "-1";
        }
    }
}

int _prompt_int(String prompt, int timeout){
    String res = _prompt(prompt, ' ', timeout);
    int res_i = res.toInt();
    //NOTE: toInt() returns 0 if cannot parse, therefore 0 is not a valid input
    return res_i;
}

bool _prompt_bool(String prompt){
    String opt = _prompt(prompt + " y/n");
    return CHAROPT(opt[0], 'y');
}

int _print_menu(String * menu_list, int menu_size, int timeout){
    int i, opt;
    while(true){
        for(i=0; i<menu_size; i++){
            O(i+1); O(": "); OL(menu_list[i]);
        }
        opt = _prompt_int("", timeout);

        if(timeout == 0 && (opt < 1 || opt > menu_size))
            OFL("Invalid Menu Option!");
        else
            return opt;
    }
}
