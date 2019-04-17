/*
 * Copyright (C) 2019 Jolla Ltd.
 * Copyright (C) 2019 Slava Monich <slava.monich@jolla.com>
 *
 * You may use this file under the terms of BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *   3. Neither the names of the copyright holders nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "ril_binder_mtk_radio.h"
#include "ril_binder_radio_impl.h"

#include <radio_instance.h>

#include <gbinder.h>

#include <grilio_encode.h>

#include <gutil_idlepool.h>
#include <gutil_log.h>
#include <gutil_misc.h>

#include <ofono/log.h>

typedef RilBinderRadioClass RilBinderMtkRadioClass;

typedef struct ril_binder_mtk_radio {
    RilBinderRadio parent;
    GBinderClient* radio_mtk_2_0;
    GBinderClient* radio_mtk_2_6;
    GBinderLocalObject* response;
    GBinderLocalObject* indication;
    GUtilIdlePool* idle;
} RilBinderMtkRadio;

#define RIL_TYPE_BINDER_MTK_RADIO (ril_binder_mtk_radio_get_type())
#define RIL_BINDER_MTK_RADIO(obj) G_TYPE_CHECK_INSTANCE_CAST((obj), \
        RIL_TYPE_BINDER_MTK_RADIO, RilBinderMtkRadio)

G_DEFINE_TYPE(RilBinderMtkRadio, ril_binder_mtk_radio, RIL_TYPE_BINDER_RADIO)

#define PARENT_CLASS ril_binder_mtk_radio_parent_class

#define DBG_(self,fmt,args...) \
    DBG("%s" fmt, (self)->parent.parent.log_prefix, ##args)

#define RADIO_IFACE_1_1(x)         RADIO_IFACE_PREFIX "1.1::" x
#define RADIO_RESPONSE_1_1         RADIO_IFACE_1_1("IRadioResponse")
#define RADIO_INDICATION_1_1       RADIO_IFACE_1_1("IRadioIndication")

#define RADIO_MTK_IFACE_PREFIX     "vendor.mediatek.hardware.radio@"
#define RADIO_MTK_IFACE_2_0(x)     RADIO_MTK_IFACE_PREFIX "2.0::" x
#define RADIO_MTK_IFACE_2_1(x)     RADIO_MTK_IFACE_PREFIX "2.1::" x
#define RADIO_MTK_IFACE_2_2(x)     RADIO_MTK_IFACE_PREFIX "2.2::" x
#define RADIO_MTK_IFACE_2_3(x)     RADIO_MTK_IFACE_PREFIX "2.3::" x
#define RADIO_MTK_IFACE_2_4(x)     RADIO_MTK_IFACE_PREFIX "2.4::" x
#define RADIO_MTK_IFACE_2_5(x)     RADIO_MTK_IFACE_PREFIX "2.5::" x
#define RADIO_MTK_IFACE_2_6(x)     RADIO_MTK_IFACE_PREFIX "2.6::" x
#define RADIO_MTK_2_0              RADIO_MTK_IFACE_2_0("IRadio")
#define RADIO_MTK_2_6              RADIO_MTK_IFACE_2_6("IRadio")
#define RADIO_MTK_RESPONSE_2_0     RADIO_MTK_IFACE_2_0("IRadioResponse")
#define RADIO_MTK_RESPONSE_2_1     RADIO_MTK_IFACE_2_1("IRadioResponse")
#define RADIO_MTK_RESPONSE_2_2     RADIO_MTK_IFACE_2_2("IRadioResponse")
#define RADIO_MTK_RESPONSE_2_3     RADIO_MTK_IFACE_2_3("IRadioResponse")
#define RADIO_MTK_RESPONSE_2_4     RADIO_MTK_IFACE_2_4("IRadioResponse")
#define RADIO_MTK_RESPONSE_2_5     RADIO_MTK_IFACE_2_5("IRadioResponse")
#define RADIO_MTK_RESPONSE_2_6     RADIO_MTK_IFACE_2_6("IRadioResponse")
#define RADIO_MTK_INDICATION_2_0   RADIO_MTK_IFACE_2_0("IRadioIndication")
#define RADIO_MTK_INDICATION_2_1   RADIO_MTK_IFACE_2_1("IRadioIndication")
#define RADIO_MTK_INDICATION_2_2   RADIO_MTK_IFACE_2_2("IRadioIndication")
#define RADIO_MTK_INDICATION_2_3   RADIO_MTK_IFACE_2_3("IRadioIndication")
#define RADIO_MTK_INDICATION_2_4   RADIO_MTK_IFACE_2_4("IRadioIndication")
#define RADIO_MTK_INDICATION_2_5   RADIO_MTK_IFACE_2_5("IRadioIndication")
#define RADIO_MTK_INDICATION_2_6   RADIO_MTK_IFACE_2_6("IRadioIndication")

typedef struct radio_mtk_data_call {
    RadioDataCall dataCall RADIO_ALIGNED(8);
    gint32 rat RADIO_ALIGNED(4);
} RADIO_ALIGNED(8) RadioMtkDataCall;
G_STATIC_ASSERT(sizeof(RadioMtkDataCall) == 128);

typedef struct radio_mtk_incoming_call_notification {
    GBinderHidlString callId RADIO_ALIGNED(8);
    GBinderHidlString number RADIO_ALIGNED(8);
    GBinderHidlString type RADIO_ALIGNED(8);
    GBinderHidlString callMode RADIO_ALIGNED(8);
    GBinderHidlString seqNo RADIO_ALIGNED(8);
    GBinderHidlString redirectNumber RADIO_ALIGNED(8);
    GBinderHidlString toNumber RADIO_ALIGNED(8);
} RADIO_ALIGNED(8) RadioMtkIncomingCallNotification;
G_STATIC_ASSERT(sizeof(RadioMtkIncomingCallNotification) == 112);

/* c(req,resp,callName,CALL_NAME) */
#define RADIO_MTK_CALL_2_0(c) \
    c(139,136,setTrm,SET_TRM) \
    c(140,137,getATR,GET_ATR) \
    c(141,138,setSimPower,SET_SIM_POWER) \
    c(142,140,hangupAll,HANGUP_ALL) \
    c(143,141,setCallIndication,SET_CALL_INDICATION) \
    c(144,142,emergencyDial,EMERGENCY_DIAL) \
    c(145,143,setEccServiceCategory,SET_ECC_SERVICE_CATEGORY) \
    c(146,144,setEccList,SET_ECC_LIST) \
    c(147,145,vtDial,VT_DIAL) \
    c(148,146,voiceAccept,VOICE_ACCEPT) \
    c(149,147,replaceVtCall,REPLACE_VT_CALL) \
    c(150,151,currentStatus,CURRENT_STATUS) \
    c(151,152,eccPreferredRat,ECC_PREFERRED_RAT) \
    /*c(152,,videoCallAccept,VIDEO_CALL_ACCEPT)*/\
    /*c(153,,imsEctCommand,IMS_ECT_COMMAND)*/\
    /*c(154,,holdCall,HOLD_CALL)*/\
    /*c(155,,resumeCall,RESUME_CALL)*/\
    /*c(156,,imsDeregNotification,IMS_DEREG_NOTIFICATION)*/\
    /*c(157,,setImsEnable,SET_IMS_ENABLE)*/\
    /*c(158,,setVolteEnable,SET_VOLTE_ENABLE)*/\
    /*c(159,,setWfcEnable,SET_WFC_ENABLE)*/\
    /*c(160,,setVilteEnable,SET_VILTE_ENABLE)*/\
    /*c(161,,setViWifiEnable,SET_VIWIFI_ENABLE)*/\
    /*c(162,,setImsVoiceEnable,SET_IMS_VOICE_ENABLE)*/\
    /*c(163,,setImsVideoEnable,SET_IMS_VIDEO_ENABLE)*/\
    /*c(164,,setImscfg,SET_IMS_CFG)*/\
    /*c(165,,getProvisionValue,GET_PROVISION_VALUE)*/\
    /*c(166,,setProvisionValue,SET_PROVISION_VALUE)*/\
    /*c(167,,addImsConferenceCallMember,ADD_IMS_CONFERENCE_CALL_MEMBER)*/\
    /*c(168,,removeImsConferenceCallMember,REMOVE_IMS_CONFERENCE_CALL_MEMBER)*/\
    /*c(169,,setWfcProfile,SET_WFC_PROFILE)*/\
    /*c(170,,conferenceDial,CONFERENCE_DIAL)*/\
    /*c(171,,vtDialWithSipUri,VT_DIAL_WITH_SIP_URI)*/\
    /*c(172,,dialWithSipUri,DIAL_WITH_SIP_URI)*/\
    /*c(173,,sendUssi,SEND_USSI)*/\
    /*c(174,,cancelUssi,CANCEL_USSI)*/\
    /*c(175,,forceReleaseCall,FORCE_RELEASE_CALL)*/\
    /*c(176,,imsBearerActivationDone,IMS_BEARER_ACTIVATION_DONE)*/\
    /*c(177,,imsBearerDeactivationDone,IMS_BEARER_DEACTIVATION_DONE)*/\
    /*c(178,,setImsRtpReport,SET_IMS_RTP_REPORT)*/\
    /*c(179,,pullCall,PULL_CALL)*/\
    /*c(180,,setImsRegistrationReport,SET_IMS_REGISTRATION_REPORT)*/\
    /*c(181,,setResponseFunctionsForAtci,SET_RESPONSE_FUNCTIONS_FOR_ATCI)*/\
    /*c(182,,sendAtciRequest,SEND_ATCI_REQUEST)*/\
    c(183,139,setModemPower,SET_MODEM_POWER) \
    c(184,148,setNetworkSelectionModeManualWithAct,SET_NETWORK_SELECTION_MODE_MANUAL_WITH_ACT) \
    c(185,149,getAvailableNetworksWithAct,GET_AVAILABLE_NETWORKS_WITH_ACT) \
    c(186,150,cancelAvailableNetworks,CANCEL_AVAILABLE_NETWORKS) \
    c(187,153,getSmsParameters,GET_SMS_PARAMETERS) \
    c(188,154,setSmsParameters,SET_SMS_PARAMETERS) \
    c(189,155,getSmsMemStatus,GET_SMS_MEM_STATUS) \
    c(190,156,setEtws,SET_ETWS) \
    c(191,157,removeCbMsg,REMOVE_CB_MSG) \
    c(192,158,setGsmBroadcastLangs,SET_GSM_BROADCAST_LANGS) \
    c(193,159,getGsmBroadcastLangs,GET_GSM_BROADCAST_LANGS) \
    c(194,160,getGsmBroadcastActivation,GET_GSM_BROADCAST_ACTIVATION) \
    c(195,161,sendEmbmsAtCommand,SEND_EMBMS_AT_COMMAND) \
    c(196,164,setApcMode,SET_APC_MODE) \
    c(197,165,getApcInfo,GET_APC_INFO) \
    c(198,166,triggerModeSwitchByEcc,TRIGGER_MODE_SWITCH_BY_ECC) \
    c(199,167,getSmsRuimMemoryStatus,GET_SMS_RUIM_MEMORY_STATUS) \
    c(200,168,setFdMode,SET_FD_MODE) \
    c(201,169,setResumeRegistration,SET_RESUME_REGISTRATION) \
    c(202,170,storeModemType,STORE_MODEM_TYPE) \
    c(203,171,reloadModemType,RELOAD_MODEM_TYPE) \
    c(204,101,setInitialAttachApnEx,SET_INITIAL_ATTACH_APN_EX) \
    c(205,172,handleStkCallSetupRequestFromSimWithResCode,HANDLE_STK_CALL_SETUP_REQUEST_FROM_SIM_WITH_RES_CODE) \
    c(206,173,getFemtocellList,GET_FEMTOCELL_LIST) \
    c(207,174,abortFemtocellList,ABORT_FEMTOCELL_LIST) \
    c(208,175,selectFemtocell,SELECT_FEMTOCELL) \
    c(209,176,queryFemtoCellSystemSelectionMode,QUERY_FEMTOCELL_SYSTEM_SELECTION_MODE) \
    c(210,177,setFemtoCellSystemSelectionMode,SET_FEMTOCELL_SYSTEM_SELECTION_MODE) \
    c(211,42,setBarringPasswordCheckedByNW,SET_BARRING_PASSWORD_CHECKED_BY_NW) \
    c(212,178,setClip,SET_CLIP) \
    c(213,179,getColp,GET_COLP) \
    c(214,180,getColr,GET_COLR) \
    c(215,181,sendCnap,SEND_CNAP) \
    c(216,182,setColp,SET_COLP) \
    c(217,183,setColr,SET_COLR) \
    c(218,184,queryCallForwardInTimeSlotStatus,QUERY_CALL_FORWARD_IN_TIME_SLOT_STATUS) \
    c(219,185,setCallForwardInTimeSlot,SET_CALL_FORWARD_IN_TIME_SLOT) \
    c(220,186,runGbaAuthentication,RUN_GBA_AUTHENTICATION) \
    c(221,187,queryPhbStorageInfo,QUERY_PHB_STORAGE_INFO) \
    c(222,188,writePhbEntry,WRITE_PHB_ENTRY) \
    c(223,189,readPhbEntry,READ_PHB_ENTRY) \
    c(224,190,queryUPBCapability,QUERY_UPB_CAPABILITY) \
    c(225,191,editUPBEntry,EDIT_UPB_ENTRY) \
    c(226,192,deleteUPBEntry,DELETE_UPB_ENTRY) \
    c(227,193,readUPBGasList,READ_UPBGAS_LIST) \
    c(228,194,readUPBGrpEntry,READ_UPBGRP_ENTRY) \
    c(229,195,writeUPBGrpEntry,WRITE_UPBGRP_ENTRY) \
    c(230,196,getPhoneBookStringsLength,GET_PHONE_BOOK_STRINGS_LENGTH) \
    c(231,197,getPhoneBookMemStorage,GET_PHONE_BOOK_MEM_STORAGE) \
    c(232,198,setPhoneBookMemStorage,SET_PHONE_BOOK_MEM_STORAGE) \
    c(233,199,readPhoneBookEntryExt,READ_PHONE_BOOK_ENTRY_EXT) \
    c(234,200,writePhoneBookEntryExt,WRITE_PHONE_BOOK_ENTRY_EXT) \
    c(235,201,queryUPBAvailable,QUERY_UPB_AVAILABLE) \
    c(236,202,readUPBEmailEntry,READ_UPB_EMAIL_ENTRY) \
    c(237,203,readUPBSneEntry,READ_UPB_SNE_ENTRY) \
    c(238,204,readUPBAnrEntry,READ_UPB_ANR_ENTRY) \
    c(239,205,readUPBAasList,READ_UPB_AAS_LIST) \
    /*c(240,,doGeneralSimAuthentication,DO_GENERAL_SIM_AUTHENTICATION)*/\
    c(241,206,queryNetworkLock,QUERY_NETWORK_LOCK) \
    c(242,207,setNetworkLock,SET_NETWORK_LOCK) \
    c(243,208,resetRadio,RESET_RADIO) \
    c(244,209,syncDataSettingsToMd,SYNC_DATA_SETTINGS_TO_MD) \
    c(245,210,resetMdDataRetryCount,RESET_MD_DATA_RETRY_COUNT) \
    c(246,211,setRemoveRestrictEutranMode,SET_REMOVE_RESTRICT_EUTRAN_MODE) \
    c(247,212,setLteAccessStratumReport,SET_LTE_ACCESS_STRATUM_REPORT) \
    c(248,213,setLteUplinkDataTransfer,SET_LTE_UPLINK_DATA_TRANSFER) \
    c(249,214,setRxTestConfig,SET_RX_TEST_CONFIG) \
    c(250,215,getRxTestResult,GET_RX_TEST_RESULT) \
    c(251,216,getPOLCapability,GET_POL_CAPABILITY) \
    c(252,217,getCurrentPOLList,GET_CURRENT_POL_LIST) \
    c(253,218,setPOLEntry,SET_POL_ENTRY) \
    c(254,219,setRoamingEnable,SET_ROAMING_ENABLE) \
    c(255,220,getRoamingEnable,GET_ROAMING_ENABLE) \
    c(256,116,setDataProfileEx,SET_DATA_PROFILE_EX) \
    c(257,221,sendVsimNotification,SEND_VSIM_NOTIFICATION) \
    c(258,222,sendVsimOperation,SEND_VSIM_OPERATION) \
    /*c(259,,setVoiceDomainPreference,SET_VOICEDOMAINPREFERENCE)*/\
    /*c(260,,setModemImsCfg,SET_MODEM_IMS_CFG)*/\
    c(261,223,setWifiEnabled,SET_WIFI_ENABLED) \
    c(262,224,setWifiAssociated,SET_WIFI_ASSOCIATED) \
    c(263,225,setWifiSignalLevel,SET_WIFI_SIGNAL_LEVEL) \
    c(264,226,setWifiIpAddress,SET_WIFI_IP_ADDRESS) \
    c(265,227,setLocationInfo,SET_LOCATION_INFO) \
    c(266,228,setEmergencyAddressId,SET_EMERGENCY_ADDRESS_ID) \
    c(267,230,setE911State,SET_E911_STATE) \
    c(268,231,setServiceStateToModem,SET_SERVICE_STATE_TO_MODEM) \
    c(269,232,sendRequestRaw,SEND_REQUEST_RAW) \
    c(270,233,sendRequestStrings,SEND_REQUEST_STRINGS) \
    c(271,229,setNattKeepAliveStatus,SET_NATT_KEEP_ALIVE_STATUS)

#define RADIO_MTK_CALL_2_6(c) \
    c(277,235,setSmsFwkReady,SET_SMS_FWK_READY) \

/* e(code,eventName,EVENT_NAME) */
#define RADIO_MTK_EVENT_2_0(e) \
    e(49,incomingCallIndication,INCOMING_CALL_INDICATION) \
    e(50,cipherIndication,CIPHER_INDICATION) \
    e(51,crssIndication,CRSS_INDICATION) \
    e(52,vtStatusInfoIndication,VT_STATUS_INFO_INDICATION) \
    e(53,speechCodecInfoIndication,SPEECH_CODEC_INFO_INDICATION) \
    e(54,cdmaCallAccepted,CDMA_CALL_ACCEPTED) \
    e(55,onVirtualSimOn,ON_VIRTUAL_SIM_ON) \
    e(56,onVirtualSimOff,ON_VIRTUAL_SIM_OFF) \
    e(57,onImeiLock,ON_IMEI_LOCK) \
    e(58,onImsiRefreshDone,ON_IMSI_REFRESH_DONE) \
    e(59,newEtwsInd,NEW_ETWS_IND) \
    e(60,meSmsStorageFullInd,ME_SMS_STORAGE_FULL_IND) \
    e(61,smsReadyInd,SMS_READY_IND) \
    e(62,dataCallListChangedEx,DATA_CALL_LIST_CHANGED_EX) \
    e(63,responseCsNetworkStateChangeInd,RESPONSE_CS_NETWORK_STATE_CHANGE_IND) \
    e(64,eMBMSAtInfoIndication,EMBMS_AT_INFO_INDICATION) \
    e(65,eMBMSSessionStatusIndication,EMBMS_SESSION_STATUS_INDICATION) \
    e(66,responsePsNetworkStateChangeInd,RESPONSE_PS_NETWORK_STATE_CHANGE_IND) \
    e(67,responseInvalidSimInd,RESPONSE_INVALID_SIM_IND) \
    e(68,responseNetworkEventInd,RESPONSE_NETWORK_EVENT_IND) \
    e(69,responseModulationInfoInd,RESPONSE_MODULATION_INFO_IND) \
    e(70,dataAllowedNotification,DATA_ALLOWED_NOTIFICATION) \
    e(71,onPseudoCellInfoInd,ON_PSEUDO_CELL_INFO_IND) \
    e(72,plmnChangedIndication,PLMN_CHANGED_INDICATION) \
    e(73,registrationSuspendedIndication,REGISTRATION_SUSPENDED_INDICATION) \
    e(74,gmssRatChangedIndication,GMSS_RAT_CHANGED_INDICATION) \
    e(75,worldModeChangedIndication,WORLD_MODE_CHANGED_INDICATION) \
    e(76,resetAttachApnInd,RESET_ATTACH_APN_IND) \
    e(77,mdChangedApnInd,MD_CHANGED_APN_IND) \
    e(78,esnMeidChangeInd,ESN_MEID_CHANGE_IND) \
    e(79,responseFemtocellInfo,RESPONSE_FEMTOCELL_INFO) \
    e(80,phbReadyNotification,PHB_READY_NOTIFICATION) \
    e(81,bipProactiveCommand,BIP_PROACTIVE_COMMAND) \
    e(82,triggerOtaSP,TRIGGER_OTA_SP) \
    e(83,onStkMenuReset,ON_STK_MENU_RESET) \
    e(84,onMdDataRetryCountReset,ON_MD_DATA_RETRY_COUNT_RESET) \
    e(85,onRemoveRestrictEutran,ON_REMOVE_RESTRICT_EUTRAN) \
    e(86,onPcoStatus,ON_PCO_STATUS) \
    e(87,onLteAccessStratumStateChanged,ON_LTE_ACCESS_STRATUM_STATE_CHANGED) \
    e(88,onSimPlugIn,ON_SIM_PLUG_IN) \
    e(89,onSimPlugOut,ON_SIM_PLUG_OUT) \
    e(90,onSimMissing,ON_SIM_MISSING) \
    e(91,onSimRecovery,ON_SIM_RECOVERY) \
    e(92,onSimTrayPlugIn,ON_SIM_TRAY_PLUG_IN) \
    e(93,onSimCommonSlotNoChanged,ON_SIM_COMMON_SLOT_NO_CHANGED) \
    e(94,onSimMeLockEvent,ON_SIM_ME_LOCK_EVENT) \
    e(95,networkInfoInd,NETWORK_INFO_IND) \
    e(96,cfuStatusNotify,CFU_STATUS_NOTIFY) \
    e(97,pcoDataAfterAttached,PCO_DATA_AFTER_ATTACHED) \
    e(98,confSRVCC,CONF_SRVCC) \
    e(99,onVsimEventIndication,ON_VSIM_EVENT_INDICATION) \
    e(100,volteLteConnectionStatus,VOLTE_LTE_CONNECTION_STATUS) \
    e(101,dedicatedBearerActivationInd,DEDICATED_BEARER_ACTIVATION_IND) \
    e(102,dedicatedBearerModificationInd,DEDICATED_BEARER_MODIFICATION_IND) \
    e(103,dedicatedBearerDeactivationInd,DEDICATED_BEARER_DEACTIVATION_IND) \
    e(104,onWifiMonitoringThreshouldChanged,ON_WIFI_MONITORING_THRESHOULD_CHANGED) \
    e(105,onWifiPdnActivate,ON_WIFI_PDN_ACTIVATE) \
    e(106,onWfcPdnError,ON_WFC_PDN_ERROR) \
    e(107,onPdnHandover,ON_PDN_HANDOVER) \
    e(108,onWifiRoveout,ON_WIFI_ROVEOUT) \
    e(109,onLocationRequest,ON_LOCATION_REQUEST) \
    e(110,onWfcPdnStateChanged,ON_WFC_PDN_STATE_CHANGED) \
    e(111,onNattKeepAliveChanged,ON_NATT_KEEP_ALIVE_CHANGED) \
    e(112,oemHookRaw,OEM_HOOK_RAW)

typedef enum radio_mtk_req {
    RADIO_MTK_REQ_SET_RESPONSE_FUNCTIONS_MTK = 137, /*setResponseFunctionsMtk*/
    RADIO_MTK_REQ_SET_RESPONSE_FUNCTIONS_IMS = 138, /*setResponseFunctionsIms*/
#define RADIO_MTK_REQ_(req,resp,Name,NAME) RADIO_MTK_REQ_##NAME = req,
    RADIO_MTK_CALL_2_0(RADIO_MTK_REQ_)
    RADIO_MTK_CALL_2_6(RADIO_MTK_REQ_)
#undef RADIO_MTK_REQ_
} RADIO_MTK_REQ;

typedef enum radio_mtk_resp {
    RADIO_MTK_RESP_SETUP_DATA_CALL = 162,    /* setupDataCallResponseEx */
    RADIO_MTK_RESP_GET_DATA_CALL_LIST = 163, /* getDataCallListResponseEx */
#define RADIO_MTK_RESP_(req,resp,Name,NAME) RADIO_MTK_RESP_##NAME = resp,
    RADIO_MTK_CALL_2_0(RADIO_MTK_RESP_)
    RADIO_MTK_CALL_2_6(RADIO_MTK_RESP_)
#undef RADIO_MTK_RESP_
} RADIO_MTK_RESP;

typedef enum radio_mtk_ind {
#define RADIO_MTK_IND_(code,Name,NAME) RADIO_MTK_IND_##NAME = code,
    RADIO_MTK_EVENT_2_0(RADIO_MTK_IND_)
#undef RADIO_MTK_IND_
} RADIO_MTK_IND;

/*==========================================================================*
 * Names
 *==========================================================================*/

static
const char*
ril_radio_mtk_resp_name(
    RADIO_MTK_RESP resp)
{
    switch (resp) {
    case RADIO_MTK_RESP_SETUP_DATA_CALL:
        return "setupDataCallResponseEx";
    case RADIO_MTK_RESP_GET_DATA_CALL_LIST:
        return "getDataCallListResponseEx";
#define RADIO_MTK_RESP_(req,resp,Name,NAME) \
    case RADIO_MTK_RESP_##NAME: return #Name "Response";
    RADIO_MTK_CALL_2_0(RADIO_MTK_RESP_)
    RADIO_MTK_CALL_2_6(RADIO_MTK_RESP_)
#undef RADIO_MTK_RESP_
    }
    return NULL;
}

static
const char*
ril_radio_mtk_ind_name(
    RADIO_MTK_IND ind)
{
    switch (ind) {
#define RADIO_MTK_IND_(code,Name,NAME) \
    case RADIO_MTK_IND_##NAME: return #Name;
    RADIO_MTK_EVENT_2_0(RADIO_MTK_IND_)
#undef RADIO_MTK_IND_
    }
    return NULL;
}

static
const char*
ril_binder_mtk_radio_resp_name(
    RilBinderMtkRadio* self,
    RADIO_MTK_RESP resp)
{
    const char* known = ril_radio_mtk_resp_name(resp);

    if (known) {
        return known;
    } else if (G_LIKELY(self)) {
        char* str = g_strdup_printf("%u", (guint)resp);

        gutil_idle_pool_add(self->idle, str, g_free);
        return str;
    } else {
        return NULL;
    }
}

static
const char*
ril_binder_mtk_radio_ind_name(
    RilBinderMtkRadio* self,
    RADIO_MTK_IND ind)
{
    const char* known = ril_radio_mtk_ind_name(ind);

    if (known) {
        return known;
    } else if (G_LIKELY(self)) {
        char* str = g_strdup_printf("%u", (guint)ind);

        gutil_idle_pool_add(self->idle, str, g_free);
        return str;
    } else {
        return NULL;
    }
}

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
gboolean
ril_binder_mtk_radio_handle_incoming_call_notification(
    RilBinderMtkRadio* self,
    RADIO_IND_TYPE type,
    GBinderReader* reader)
{
    const RadioMtkIncomingCallNotification* call =
        gbinder_reader_read_hidl_struct(reader,
            RadioMtkIncomingCallNotification);

    if (call) {
        int cid, seq;

        DBG_(self, "call %s from %s", call->callId.data.str,
            call->number.data.str);
        if (gutil_parse_int(call->callId.data.str, 10, &cid) &&
            gutil_parse_int(call->seqNo.data.str, 10, &seq)) {
            GBinderClient* client = self->radio_mtk_2_0;
            GBinderLocalRequest* req = gbinder_client_new_request(client);
            GBinderWriter writer;
            guint32 serial = 0xbadf00d;
#pragma message("TODO: request unique request id from libgrilio")

            gbinder_local_request_init_writer(req, &writer);
            gbinder_writer_append_int32(&writer, serial);
            gbinder_writer_append_int32(&writer, 0 /* allow */);
            gbinder_writer_append_int32(&writer, cid);
            gbinder_writer_append_int32(&writer, seq);
            gbinder_client_transact(client, RADIO_MTK_REQ_SET_CALL_INDICATION,
                GBINDER_TX_FLAG_ONEWAY, req, NULL, NULL, NULL);
            gbinder_local_request_unref(req);
            DBG_(self, "accepting call %d seq %d", cid, seq);
            return TRUE;
        }
    }
    return FALSE;
}

static
gboolean
ril_binder_mtk_radio_decode_data_call_list(
    GBinderReader* in,
    GByteArray* out)
{
    gboolean ok = FALSE;
    gsize count = 0;
    const RadioMtkDataCall* calls =
        gbinder_reader_read_hidl_type_vec(in, RadioMtkDataCall, &count);

    if (calls) {
        guint i;

        grilio_encode_int32(out, DATA_CALL_VERSION);
        grilio_encode_int32(out, count);
        for (i = 0; i < count; i++) {
            ril_binder_radio_decode_data_call(out, &calls[i].dataCall);
        }
        ok = TRUE;
    }
    return ok;
}

static
gboolean
ril_binder_mtk_radio_decode_data_call(
    GBinderReader* in,
    GByteArray* out)
{
    const RadioMtkDataCall* call =
        gbinder_reader_read_hidl_struct(in, RadioMtkDataCall);

    if (call) {
        ril_binder_radio_decode_data_call(out, &call->dataCall);
        return TRUE;
    } else {
        return FALSE;
    }
}

static
GBinderLocalReply*
ril_binder_mtk_radio_response(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* user_data)
{
    RilBinderMtkRadio* self = RIL_BINDER_MTK_RADIO(user_data);
    const char* iface = gbinder_remote_request_interface(req);

    if (!g_strcmp0(iface, RADIO_MTK_RESPONSE_2_0)) {
        /* Do we need to decode it? */
        RilBinderRadioDecodeFunc decode;

        switch (code) {
        case RADIO_MTK_RESP_GET_DATA_CALL_LIST:
            decode = ril_binder_mtk_radio_decode_data_call_list;
            break;
        case RADIO_MTK_RESP_SETUP_DATA_CALL:
            decode = ril_binder_mtk_radio_decode_data_call;
            break;
        default:
            decode = NULL;
            break;
        }

        if (decode) {
            GBinderReader reader;
            const RadioResponseInfo* info;

            /* They all start with RadioResponseInfo */
            gbinder_remote_request_init_reader(req, &reader);
            info = gbinder_reader_read_hidl_struct(&reader, RadioResponseInfo);
            if (info) {
                DBG_(self, "IRadioResponse[2.0] %u %s", code,
                    ril_binder_mtk_radio_resp_name(self, code));
                if (ril_binder_radio_decode_response(&self->parent, info,
                    decode, &reader)) {
                    *status = GBINDER_STATUS_OK;
                    return NULL;
                }
            }
        } else if (code == RADIO_MTK_RESP_SET_CALL_INDICATION) {
            GBinderReader reader;
            const RadioResponseInfo* info;

            gbinder_remote_request_init_reader(req, &reader);
            info = gbinder_reader_read_hidl_struct(&reader, RadioResponseInfo);
            if (info) {
                DBG_(self, "IRadioResponse[2.0] %u %s 0x%08x", code,
                    ril_binder_mtk_radio_resp_name(self, code), info->serial);
                *status = GBINDER_STATUS_OK;
                return NULL;
            }
        }
        ofono_warn("Unhandled MTK IRadioResponse[2.0] %s",
            ril_binder_mtk_radio_resp_name(self, code));
    } else if (!g_strcmp0(iface, RADIO_MTK_RESPONSE_2_6)) {
        if (code == RADIO_MTK_RESP_SET_SMS_FWK_READY) {
            GBinderReader reader;
            const RadioResponseInfo* info;

            gbinder_remote_request_init_reader(req, &reader);
            info = gbinder_reader_read_hidl_struct(&reader, RadioResponseInfo);
            if (info) {
                DBG_(self, "IRadioResponse[2.6] %u %s 0x%08x", code,
                    ril_binder_mtk_radio_resp_name(self, code), info->serial);
                *status = GBINDER_STATUS_OK;
                return NULL;
            }
        } else {
            ofono_warn("Unhandled MTK IRadioResponse[2.6] %s",
                ril_binder_mtk_radio_resp_name(self, code));
        }
    } else if (!g_strcmp0(iface, RADIO_RESPONSE_1_0)) {
        GBinderReader reader;
        RilBinderRadio* parent = &self->parent;
        const RadioResponseInfo* info;

        gbinder_remote_request_init_reader(req, &reader);
        info = gbinder_reader_read_hidl_struct(&reader, RadioResponseInfo);
        if (info) {
            RilBinderRadioClass* klass = RIL_BINDER_RADIO_GET_CLASS(self);

            DBG_(self, "forwarding %u %s to the base class", code,
                radio_instance_resp_name(parent->radio, code));
            if (klass->handle_response(parent, code, info, &reader)) {
                *status = GBINDER_STATUS_OK;
                return NULL;
            }
        }
        ofono_warn("Unhandled response %s", radio_instance_resp_name
            (parent->radio, code));
    } else {
        ofono_warn("Unexpected response %s %u", iface, code);
    }
    *status = GBINDER_STATUS_FAILED;
    return NULL;
}

static
GBinderLocalReply*
ril_binder_mtk_radio_indication(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* user_data)
{
    RilBinderMtkRadio* self = RIL_BINDER_MTK_RADIO(user_data);
    const char* iface = gbinder_remote_request_interface(req);

    if (!g_strcmp0(iface, RADIO_MTK_INDICATION_2_0)) {
        GBinderReader reader;
        guint type;

        gbinder_remote_request_init_reader(req, &reader);
        if (gbinder_reader_read_uint32(&reader, &type) &&
            (type == RADIO_IND_UNSOLICITED || type == RADIO_IND_ACK_EXP)) {
            switch (code) {
            case RADIO_MTK_IND_INCOMING_CALL_INDICATION:
                DBG_(self, "IRadioIndication %u %s", code,
                    ril_binder_mtk_radio_ind_name(self, code));
                if (ril_binder_mtk_radio_handle_incoming_call_notification(self,
                    type, &reader)) {
                    *status = GBINDER_STATUS_OK;
                    return NULL;
                }
                break;
            }
        }
        ofono_warn("Unhandled MTK indication %s",
            ril_binder_mtk_radio_ind_name(self, code));
    } else if (!g_strcmp0(iface, RADIO_INDICATION_1_0)) {
        RilBinderRadio* parent = &self->parent;
        GBinderReader reader;
        guint type;

        gbinder_remote_request_init_reader(req, &reader);
        if (gbinder_reader_read_uint32(&reader, &type) &&
            (type == RADIO_IND_UNSOLICITED || type == RADIO_IND_ACK_EXP)) {
            RilBinderRadioClass* klass = RIL_BINDER_RADIO_GET_CLASS(self);

            /* Forward it to the base class */
            DBG_(self, "forwarding %u %s to the base class", code,
                radio_instance_ind_name(parent->radio, code));
            if (klass->handle_indication(parent, code, type, &reader)) {
                *status = GBINDER_STATUS_OK;
                return NULL;
            }
        }
        ofono_warn("Unhandled indication %s", radio_instance_ind_name
            (parent->radio, code));
    } else {
        ofono_warn("Unexpected indication %s %u", iface, code);
    }
    *status = GBINDER_STATUS_FAILED;
    return NULL;
}

static
void
ril_binder_mtk_radio_finish_init(
    RilBinderMtkRadio* self)
{
    GBinderClient* client = self->radio_mtk_2_6;
    GBinderLocalRequest* req = gbinder_client_new_request(client);
    guint32 serial = 0xdeadbeef;
#pragma message("TODO: request unique request id from libgrilio")

    /* If we don't do this, we never get notified of incoming SMS */
    DBG_(self, "setSmsFwkReady");
    gbinder_local_request_append_int32(req, serial);
    gbinder_client_transact(client, RADIO_MTK_REQ_SET_SMS_FWK_READY,
        GBINDER_TX_FLAG_ONEWAY, req, NULL, NULL, NULL);
    gbinder_local_request_unref(req);
}

/*==========================================================================*
 * API
 *==========================================================================*/

GRilIoTransport*
ril_binder_mtk_radio_new(
    GHashTable* args)
{
    const char* dev = ril_binder_radio_arg_dev(args);
    const char* slot = ril_binder_radio_arg_name(args);
    GBinderServiceManager* sm = gbinder_servicemanager_new(dev);

    if (sm) {
        int status = 0;
        char* fqname = g_strconcat(RADIO_MTK_2_0 "/", slot, NULL);
        GBinderRemoteObject* remote = gbinder_servicemanager_get_service_sync
            (sm, fqname, &status);

        g_free(fqname);
        if (remote) {
            /*
             * MTK specific response functions have to be registered before
             * registering the standard response functions (which is done
             * by ril_binder_radio_init()
             */
            RilBinderMtkRadio* self =
                g_object_new(RIL_TYPE_BINDER_MTK_RADIO, NULL);
            RilBinderRadio* parent = &self->parent;
            GRilIoTransport* transport = &parent->parent;
            GBinderIpc* ipc = gbinder_remote_object_ipc(remote);
            GBinderLocalRequest* req;
            GBinderRemoteReply* reply;
            GBinderWriter writer;

            static const char* response_ifaces[] = {
                RADIO_MTK_RESPONSE_2_6,
                RADIO_MTK_RESPONSE_2_5,
                RADIO_MTK_RESPONSE_2_4,
                RADIO_MTK_RESPONSE_2_3,
                RADIO_MTK_RESPONSE_2_2,
                RADIO_MTK_RESPONSE_2_1,
                RADIO_MTK_RESPONSE_2_0,
                RADIO_RESPONSE_1_1,
                RADIO_RESPONSE_1_0,
                NULL
            };
            static const char* indication_ifaces[] = {
                RADIO_MTK_INDICATION_2_6,
                RADIO_MTK_INDICATION_2_5,
                RADIO_MTK_INDICATION_2_4,
                RADIO_MTK_INDICATION_2_3,
                RADIO_MTK_INDICATION_2_2,
                RADIO_MTK_INDICATION_2_1,
                RADIO_MTK_INDICATION_2_0,
                RADIO_INDICATION_1_1,
                RADIO_INDICATION_1_0,
                NULL
            };

            self->radio_mtk_2_0 = gbinder_client_new(remote, RADIO_MTK_2_0);
            self->radio_mtk_2_6 = gbinder_client_new(remote, RADIO_MTK_2_6);
            self->response = gbinder_local_object_new(ipc, response_ifaces,
                ril_binder_mtk_radio_response, self);
            self->indication = gbinder_local_object_new(ipc,
                indication_ifaces, ril_binder_mtk_radio_indication, self);

            /* IRadio::setResponseFunctionsMtk */
            req = gbinder_client_new_request(self->radio_mtk_2_0);
            gbinder_local_request_init_writer(req, &writer);
            gbinder_writer_append_local_object(&writer, self->response);
            gbinder_writer_append_local_object(&writer, self->indication);
            reply = gbinder_client_transact_sync_reply(self->radio_mtk_2_0,
                RADIO_MTK_REQ_SET_RESPONSE_FUNCTIONS_MTK, req, &status);
            gbinder_local_request_unref(req);
            gbinder_remote_reply_unref(reply);

            if (status == GBINDER_STATUS_OK) {
                ofono_info("Registered MTK callbacks for %s", slot);
                if (ril_binder_radio_init_base(parent, args)) {
                    ril_binder_mtk_radio_finish_init(self);
                    gbinder_servicemanager_unref(sm);
                    return transport;
                }
            } else {
                DBG_(self, "setResponseFunctionsMtk %s error %d", slot, status);
                ofono_error("setResponseFunctionsMtk failed");
            }
            grilio_transport_unref(transport);
        }
        gbinder_servicemanager_unref(sm);
    }
    return NULL;
}

/*==========================================================================*
 * Logging
 *==========================================================================*/

static
void
ril_binder_mtk_radio_gbinder_log_notify(
    struct ofono_debug_desc* desc)
{
    gbinder_log.level = (desc->flags & OFONO_DEBUG_FLAG_PRINT) ?
        GLOG_LEVEL_VERBOSE : GLOG_LEVEL_INHERIT;
}

static struct ofono_debug_desc gbinder_debug OFONO_DEBUG_ATTR = {
    .name = "gbinder",
    .flags = OFONO_DEBUG_FLAG_DEFAULT,
    .notify = ril_binder_mtk_radio_gbinder_log_notify
};

static
void
ril_binder_mtk_radio_gbinder_radio_log_notify(
    struct ofono_debug_desc* desc)
{
    gbinder_radio_log.level = (desc->flags & OFONO_DEBUG_FLAG_PRINT) ?
        GLOG_LEVEL_VERBOSE : GLOG_LEVEL_INHERIT;
}

static struct ofono_debug_desc gbinder_radio_debug OFONO_DEBUG_ATTR = {
    .name = "gbinder-radio",
    .flags = OFONO_DEBUG_FLAG_DEFAULT,
    .notify = ril_binder_mtk_radio_gbinder_radio_log_notify
};

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
ril_binder_mtk_radio_init(
    RilBinderMtkRadio* self)
{
    self->idle = gutil_idle_pool_new();
}

static
void
ril_binder_mtk_radio_finalize(
    GObject* object)
{
    RilBinderMtkRadio* self = RIL_BINDER_MTK_RADIO(object);

    gutil_idle_pool_destroy(self->idle);
    if (self->indication) {
        gbinder_local_object_drop(self->indication);
        self->indication = NULL;
    }
    if (self->response) {
        gbinder_local_object_drop(self->response);
        self->response = NULL;
    }
    gbinder_client_unref(self->radio_mtk_2_0);
    gbinder_client_unref(self->radio_mtk_2_6);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
ril_binder_mtk_radio_class_init(
    RilBinderMtkRadioClass* klass)
{
    G_OBJECT_CLASS(klass)->finalize = ril_binder_mtk_radio_finalize;
}


/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
