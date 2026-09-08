/*
 * evse_bridge.cpp - Globals <-> evse_ctx_t synchronization and HAL wiring
 *
 * This bridge layer copies firmware globals into the module's context struct
 * before each call, and copies results back afterward. This is the least
 * disruptive integration: consumer files keep reading/writing globals, and
 * the module operates on its own struct.
 *
 * Only compiled for CH32 and v3 ESP32 builds where the state machine runs
 * locally (v4 ESP32 delegates to CH32 via serial).
 */

#include "main.h"
#include "meter.h"
#include "esp32.h"
#include "evse_bridge.h"
#include "capacity_peak.h"

// Only build bridge for platforms that run the state machine locally

// ---- Global context instance ----
evse_ctx_t g_evse_ctx;

// ---- Spinlock for sync functions ----
static portMUX_TYPE evse_sync_spinlock = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t evse_ctx_mutex = NULL;

// ---- External references to firmware globals ----
// (Most are declared in main.h / main_c.h or as file-scope in main.cpp)
extern uint8_t PIN_ACTA, PIN_ACTB;  // Dynamically assigned in esp32.cpp
extern uint8_t State;
extern uint8_t Mode;
extern uint8_t LoadBl;
extern uint8_t Config;
extern AccessStatus_t AccessStatus;
extern uint8_t RFIDReader;
extern bool CPDutyOverride;
extern uint16_t MaxMains;
extern uint16_t MaxCurrent;
extern uint16_t MinCurrent;
extern uint16_t MaxCircuit;
extern uint16_t MaxCapacity;
extern uint16_t MaxSumMains;
extern uint8_t MaxSumMainsTime;
extern uint16_t MaxSumMainsTimer;
extern uint16_t GridRelayMaxSumMains;
extern bool GridRelayOpen;
extern uint16_t Balanced[];
extern uint16_t BalancedMax[];
extern uint8_t BalancedState[];
extern uint16_t BalancedError[];
extern uint16_t ChargeCurrent;
extern int16_t IsetBalanced;
extern uint16_t OverrideCurrent;
extern int16_t Isum;
extern uint8_t ErrorFlags;
extern uint8_t ChargeDelay;
extern uint8_t NoCurrent;
extern uint16_t SolarStopTimer;
extern uint8_t AccessTimer;
extern EnableC2_t EnableC2;
extern uint8_t Nr_Of_Phases_Charging;
extern Switch_Phase_t Switching_Phases_C2;
extern bool phasesLastUpdateFlag;
extern uint16_t StartCurrent;
extern uint16_t StopTime;
extern uint16_t ImportCurrent;
extern int8_t TempEVSE;
extern uint16_t maxTemp;
extern uint8_t RCmon;
extern uint8_t ActivationMode;
extern uint8_t ActivationTimer;
extern uint8_t PrioStrategy;
extern uint16_t RotationInterval;
extern uint16_t IdleTimeout;
extern uint32_t ConnectedTime[];
extern uint8_t ScheduleState[];
extern uint16_t RotationTimer;
extern Node_t Node[];
extern Meter MainsMeter;
extern Meter EVMeter;
extern int16_t CapacityHeadroom_da;
extern Meter CircuitMeter;
extern uint16_t MaxCircuitMains;

// These are inside the #if CH32/v3 guard in main.cpp
extern uint8_t C1Timer;
extern uint8_t ModemStage;
extern int8_t DisconnectTimeCounter;
extern uint8_t ToModemWaitStateTimer;
extern uint8_t ToModemDoneStateTimer;
extern uint8_t LeaveModemDoneStateTimer;
extern uint8_t LeaveModemDeniedStateTimer;
extern bool PilotDisconnected;
extern uint8_t PilotDisconnectTime;

extern uint8_t OcppMode;
extern float OcppCurrentLimit;

// Additional externs for state-change callback and bridge functions
extern const char StrStateName[15][13];
extern void setSolarStopTimer(uint16_t Timer);
extern void setChargeDelay(uint8_t delay);
extern struct tm timeinfo;
extern hw_timer_t *timerA;
extern uint8_t LCDTimer;
extern uint16_t BacklightTimer;
extern uint8_t LCDNav;
extern void GLCD_init();
extern void GLCD(void);
#if MQTT
extern uint8_t lastMqttUpdate;
#endif
extern void request_write_settings(void);

// ---- HAL callbacks ----
// Map module's HAL function pointers to firmware hardware

static void hal_set_cp_duty(uint32_t duty) {
    SetCPDuty(duty);
}

static void hal_contactor1(bool on) {
    if (on) {
        CONTACTOR1_ON;
    } else {
        CONTACTOR1_OFF;
    }
}

static void hal_contactor2(bool on) {
    if (on) {
        CONTACTOR2_ON;
    } else {
        CONTACTOR2_OFF;
    }
}

static void hal_set_pilot(bool on) {
    setPilot(on);
}

static void hal_actuator_lock(void) {
    ACTUATOR_LOCK;
}

static void hal_actuator_unlock(void) {
    ACTUATOR_UNLOCK;
}

static void hal_actuator_off(void) {
    ACTUATOR_OFF;
}

// ---- State change callback ----
// Fires from inside evse_set_state() to handle platform-specific post-actions.
// At this point g_evse_ctx has updated values; globals may still have old values.
static void hal_on_state_change(uint8_t old_state, uint8_t new_state) {
    // === LOGGING ===
    if (old_state != new_state) {
        char Str[50];
        snprintf(Str, sizeof(Str), "%02d:%02d:%02d STATE %s -> %s\n",
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                 StrStateName[old_state], StrStateName[new_state]);
        _LOG_A("%s", Str);
    }

    // === PER-STATE PLATFORM ACTIONS ===
    switch (new_state) {
        case STATE_B1:
            if (old_state != STATE_B1 && !PilotDisconnected && g_evse_ctx.PilotDisconnected) {
                _LOG_A("Pilot Disconnected\n");
            }
            // fall through
        case STATE_A:
            timerAlarmWrite(timerA, PWM_100, true);
            timerAlarmEnable(timerA);                                   // Re-enable in case STATE_B's single-shot (auto_reload=false) fired and disabled the alarm — otherwise CP sampling stops and cable disconnect in PAUSE is not detected (upstream e6110b1, fixes #347)
            break;

        case STATE_B:
            // Reset timer counter before setting alarm to ensure the alarm
            // fires even after long periods without CP pulses (e.g., after
            // ACTSTART where 0% duty means no rising edges to reset timer).
            // Without this, the 64-bit timer counter can be far past the
            // alarm value, causing the alarm to never fire.
            timerWrite(timerA, 0);
            timerAlarmWrite(timerA, PWM_95, false);
            break;

        case STATE_C:
            LCDTimer = 0;
            {
                uint8_t nrPhases = g_evse_ctx.Nr_Of_Phases_Charging;
                Nr_Of_Phases_Charging = nrPhases;
                SEND_TO_ESP32(Nr_Of_Phases_Charging);
            }
            {
                uint16_t newSolarStopTimer = g_evse_ctx.SolarStopTimer;
                setSolarStopTimer(newSolarStopTimer);
            }
            break;

        case STATE_C1:
            timerAlarmWrite(timerA, PWM_100, true);
            timerAlarmEnable(timerA);                                   // See STATE_A note (upstream e6110b1)
            break;

        default:
            break;
    }

    // === LCD REFRESH (v3 ESP32 only) ===
    if (old_state == STATE_C || old_state == STATE_C1) {
        GLCD_init();
    } else if (new_state == STATE_C && old_state != new_state) {
        if (!LCDNav) GLCD();
    }

    // === COMMON POST-ACTIONS ===
#if MQTT
    lastMqttUpdate = 10;
#endif
    BacklightTimer = BACKLIGHT;
}

// ---- Sync: globals -> ctx ----
void evse_sync_globals_to_ctx(void) {
    portENTER_CRITICAL(&evse_sync_spinlock);
    evse_ctx_t *ctx = &g_evse_ctx;

    ctx->State = State;
    ctx->Mode = Mode;
    ctx->LoadBl = LoadBl;
    ctx->Config = Config;
    ctx->AccessStatus = (AccessStatus_t)AccessStatus;
    ctx->RFIDReader = RFIDReader;
    ctx->CPDutyOverride = CPDutyOverride;

    ctx->OcppMode = OcppMode;
    ctx->OcppCurrentLimit = OcppCurrentLimit;

    ctx->MaxMains = MaxMains;
    ctx->MaxCurrent = MaxCurrent;
    ctx->MinCurrent = MinCurrent;
    ctx->MaxCircuit = MaxCircuit;
    ctx->MaxCapacity = MaxCapacity;
    ctx->MaxSumMains = MaxSumMains;
    ctx->MaxSumMainsTime = MaxSumMainsTime;
    ctx->GridRelayMaxSumMains = GridRelayMaxSumMains;
    ctx->GridRelayOpen = GridRelayOpen;

    for (int i = 0; i < NR_EVSES; i++) {
        ctx->Balanced[i] = Balanced[i];
        ctx->BalancedMax[i] = BalancedMax[i];
        ctx->BalancedState[i] = BalancedState[i];
        ctx->BalancedError[i] = BalancedError[i];
    }
    ctx->ChargeCurrent = ChargeCurrent;
    ctx->IsetBalanced = IsetBalanced;
    ctx->OverrideCurrent = OverrideCurrent;

    ctx->Isum = Isum;
    ctx->MainsMeterImeasured = MainsMeter.Imeasured;
    ctx->MainsMeterIrms[0] = MainsMeter.Irms[0];
    ctx->MainsMeterIrms[1] = MainsMeter.Irms[1];
    ctx->MainsMeterIrms[2] = MainsMeter.Irms[2];
    ctx->EVMeterImeasured = EVMeter.Imeasured;
    ctx->EVMeterIrms[0] = EVMeter.Irms[0];
    ctx->EVMeterIrms[1] = EVMeter.Irms[1];
    ctx->EVMeterIrms[2] = EVMeter.Irms[2];
    ctx->MainsMeterType = MainsMeter.Type;
    ctx->EVMeterType = EVMeter.Type;
    ctx->MainsMeterTimeout = MainsMeter.Timeout;
    ctx->EVMeterTimeout = EVMeter.Timeout;

    // CircuitMeter: sync max of 3 phases and configured limit
    if (CircuitMeter.Type) {
        int16_t cmax = CircuitMeter.Irms[0];
        if (CircuitMeter.Irms[1] > cmax) cmax = CircuitMeter.Irms[1];
        if (CircuitMeter.Irms[2] > cmax) cmax = CircuitMeter.Irms[2];
        ctx->CircuitMeterImeasured = cmax;
    } else {
        ctx->CircuitMeterImeasured = 0;
    }
    ctx->MaxCircuitMains = MaxCircuitMains;

    ctx->ErrorFlags = ErrorFlags;
    ctx->ChargeDelay = ChargeDelay;
    ctx->NoCurrent = NoCurrent;

    ctx->SolarStopTimer = SolarStopTimer;
    ctx->MaxSumMainsTimer = MaxSumMainsTimer;
    ctx->AccessTimer = AccessTimer;
    ctx->C1Timer = C1Timer;

    ctx->EnableC2 = (EnableC2_t)EnableC2;
    ctx->Nr_Of_Phases_Charging = Nr_Of_Phases_Charging;
    ctx->Switching_Phases_C2 = (uint8_t)Switching_Phases_C2;
    ctx->phasesLastUpdateFlag = phasesLastUpdateFlag;

    ctx->ModemEnabled = MODEM;
    ctx->ModemStage = ModemStage;
    ctx->DisconnectTimeCounter = DisconnectTimeCounter;
    ctx->ToModemWaitStateTimer = ToModemWaitStateTimer;
    ctx->ToModemDoneStateTimer = ToModemDoneStateTimer;
    ctx->LeaveModemDoneStateTimer = LeaveModemDoneStateTimer;
    ctx->LeaveModemDeniedStateTimer = LeaveModemDeniedStateTimer;

    ctx->PilotDisconnected = PilotDisconnected;
    ctx->PilotDisconnectTime = PilotDisconnectTime;

    ctx->StartCurrent = StartCurrent;
    ctx->StopTime = StopTime;
    ctx->ImportCurrent = ImportCurrent;

    ctx->TempEVSE = TempEVSE;
    ctx->maxTemp = maxTemp;
    ctx->CapacityHeadroom_da = CapacityHeadroom_da;
    ctx->RCmon = RCmon;

    ctx->ActivationMode = ActivationMode;
    ctx->ActivationTimer = ActivationTimer;

    ctx->PrioStrategy = PrioStrategy;
    ctx->RotationInterval = RotationInterval;
    ctx->IdleTimeout = IdleTimeout;
    ctx->RotationTimer = RotationTimer;
    for (int i = 0; i < NR_EVSES; i++) {
        ctx->ConnectedTime[i] = ConnectedTime[i];
        ctx->ScheduleState[i] = ScheduleState[i];
    }

    for (int i = 0; i < NR_EVSES; i++) {
        ctx->Node[i].Online = Node[i].Online;
        ctx->Node[i].ConfigChanged = Node[i].ConfigChanged;
        ctx->Node[i].EVMeter = Node[i].EVMeter;
        ctx->Node[i].EVAddress = Node[i].EVAddress;
        ctx->Node[i].MinCurrent = Node[i].MinCurrent;
        ctx->Node[i].Phases = Node[i].Phases;
        ctx->Node[i].Timer = Node[i].Timer;
        ctx->Node[i].IntTimer = Node[i].IntTimer;
        ctx->Node[i].SolarTimer = Node[i].SolarTimer;
        ctx->Node[i].Mode = Node[i].Mode;
    }
    portEXIT_CRITICAL(&evse_sync_spinlock);
}

// ---- Sync: ctx -> globals ----
void evse_sync_ctx_to_globals(void) {
    portENTER_CRITICAL(&evse_sync_spinlock);
    evse_ctx_t *ctx = &g_evse_ctx;

    State = ctx->State;
    Mode = ctx->Mode;
    AccessStatus = (AccessStatus_t)ctx->AccessStatus;

    ErrorFlags = ctx->ErrorFlags;
    ChargeDelay = ctx->ChargeDelay;
    NoCurrent = ctx->NoCurrent;

    for (int i = 0; i < NR_EVSES; i++) {
        Balanced[i] = ctx->Balanced[i];
        BalancedMax[i] = ctx->BalancedMax[i];
        BalancedState[i] = ctx->BalancedState[i];
        BalancedError[i] = ctx->BalancedError[i];
    }
    ChargeCurrent = ctx->ChargeCurrent;
    IsetBalanced = (int16_t)ctx->IsetBalanced;
    OverrideCurrent = ctx->OverrideCurrent;

    SolarStopTimer = ctx->SolarStopTimer;
    MaxSumMainsTimer = ctx->MaxSumMainsTimer;
    AccessTimer = ctx->AccessTimer;
    C1Timer = ctx->C1Timer;

    Nr_Of_Phases_Charging = ctx->Nr_Of_Phases_Charging;
    Switching_Phases_C2 = (Switch_Phase_t)ctx->Switching_Phases_C2;
    phasesLastUpdateFlag = ctx->phasesLastUpdateFlag;

    ModemStage = ctx->ModemStage;
    DisconnectTimeCounter = ctx->DisconnectTimeCounter;
    ToModemWaitStateTimer = ctx->ToModemWaitStateTimer;
    ToModemDoneStateTimer = ctx->ToModemDoneStateTimer;
    LeaveModemDoneStateTimer = ctx->LeaveModemDoneStateTimer;
    LeaveModemDeniedStateTimer = ctx->LeaveModemDeniedStateTimer;

    PilotDisconnected = ctx->PilotDisconnected;
    PilotDisconnectTime = ctx->PilotDisconnectTime;

    ActivationMode = ctx->ActivationMode;
    ActivationTimer = ctx->ActivationTimer;

    RotationTimer = ctx->RotationTimer;
    for (int i = 0; i < NR_EVSES; i++) {
        ConnectedTime[i] = ctx->ConnectedTime[i];
        ScheduleState[i] = ctx->ScheduleState[i];
    }

    MainsMeter.Timeout = ctx->MainsMeterTimeout;
    EVMeter.Timeout = ctx->EVMeterTimeout;

    for (int i = 0; i < NR_EVSES; i++) {
        Node[i].Online = ctx->Node[i].Online;
        Node[i].ConfigChanged = ctx->Node[i].ConfigChanged;
        Node[i].EVMeter = ctx->Node[i].EVMeter;
        Node[i].EVAddress = ctx->Node[i].EVAddress;
        Node[i].MinCurrent = ctx->Node[i].MinCurrent;
        Node[i].Phases = ctx->Node[i].Phases;
        Node[i].Timer = ctx->Node[i].Timer;
        Node[i].IntTimer = ctx->Node[i].IntTimer;
        Node[i].SolarTimer = ctx->Node[i].SolarTimer;
        Node[i].Mode = ctx->Node[i].Mode;
    }
    portEXIT_CRITICAL(&evse_sync_spinlock);
}

// ---- Transaction-level lock for bridge callers ----
// Wraps the full sync_to → operate → sync_from cycle so concurrent tasks
// (Timer10ms, Timer1S, loop/OCPP) cannot corrupt g_evse_ctx mid-transaction.
void evse_bridge_lock(void) {
    xSemaphoreTake(evse_ctx_mutex, portMAX_DELAY);
}

void evse_bridge_unlock(void) {
    xSemaphoreGive(evse_ctx_mutex);
}

// ---- Solar debug snapshot reader (spinlock-protected) ----
void evse_get_solar_debug(evse_solar_debug_t *out) {
    if (!out) return;
    portENTER_CRITICAL(&evse_sync_spinlock);
    *out = g_evse_ctx.solar_debug;
    portEXIT_CRITICAL(&evse_sync_spinlock);
}

// ---- Initialization ----
void evse_bridge_init(void) {
    evse_ctx_mutex = xSemaphoreCreateMutex();
    configASSERT(evse_ctx_mutex);
    evse_hal_t hal = {
        .set_cp_duty      = hal_set_cp_duty,
        .contactor1       = hal_contactor1,
        .contactor2       = hal_contactor2,
        .set_pilot        = hal_set_pilot,
        .actuator_lock    = hal_actuator_lock,
        .actuator_unlock  = hal_actuator_unlock,
        .actuator_off     = hal_actuator_off,
        .on_state_change  = hal_on_state_change,
    };

    evse_init(&g_evse_ctx, &hal);
    evse_sync_globals_to_ctx();
}

