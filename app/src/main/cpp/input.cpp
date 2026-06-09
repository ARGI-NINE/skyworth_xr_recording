//
// Created by eryazhan on 10/18/2022.
//

#include "input.h"
#include "xr_logger.h"
#include <android/log.h>

#define LOG_TAG "Input"
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__))

void Input::UpdateInput(XrTime atTime) {
    //LOGI("UpdateInput ! ");
    UpdateHand(atTime);
    XrResult result;
    const XrActiveActionSet activeActionSet{mActionSet,0};
    XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeActionSet;
    result = xrSyncActions(mXrSession,&syncInfo);
    if(result != XR_SUCCESS)
    {
        LOGI("UpdateInput xrSyncActions failed %d",result);
        return;
    }
    // menu
    XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,mShortcutAction,0};
    XrActionStateBoolean shortcutValue{XR_TYPE_ACTION_STATE_BOOLEAN};
    result = xrGetActionStateBoolean(mXrSession,&getInfo,&shortcutValue);
    if(result != XR_SUCCESS)
    {
        LOGI("UpdateInput xrGetActionStateBoolean failed %d",result);
    }
    if((shortcutValue.isActive == XR_TRUE) && (shortcutValue.currentState == XR_TRUE))
    {
        if(!mShortcutPressed)
        {
            LOGI("UpdateInput mShortcutPressed");
            mShortcutPressed = true;
        }
    }else
    {
        if(mShortcutPressed)
        {
            LOGI("UpdateInput mShortcut up");
            mShortcutPressed = false;
        }
    }
    XrActionStateGetInfo getrightsystemInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,mRightSystemAction,0};
    XrActionStateBoolean rightsystemValue{XR_TYPE_ACTION_STATE_BOOLEAN};
    result = xrGetActionStateBoolean(mXrSession,&getrightsystemInfo,&rightsystemValue);
    if(result != XR_SUCCESS)
    {
        LOGI("UpdateInput xrGetActionStateBoolean mLeftTriggertAction failed %d",result);
    }

    if((rightsystemValue.isActive == XR_TRUE) && (rightsystemValue.currentState == XR_TRUE))
    {
        if(!mRightSystemPressed)
        {
            mRightSystemPressed = true;
            LOGI("UpdateInput rightsystemValue pressed! ");
//            openxrapp->mInstrumentationImple->SendKey(KeyAction::ACTION_DOWN,KeyCode::KEYCODE_HOME);
        }

    }
    else
    {

        if(mRightSystemPressed)
        {
            mRightSystemPressed = false;
            LOGI("UpdateInput rightsystemValue up! ");
//            openxrapp->mInstrumentationImple->SendKey(KeyAction::ACTION_UP,KeyCode::KEYCODE_HOME);
        }
    }
    //AB  XY
    XrActionStateGetInfo getleftInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,mLeftSelectAction,0};
    XrActionStateBoolean leftselectValue{XR_TYPE_ACTION_STATE_BOOLEAN};
    result = xrGetActionStateBoolean(mXrSession,&getleftInfo,&leftselectValue);
    if(result != XR_SUCCESS)
    {
        LOGI("UpdateInput xrGetActionStateBoolean mLeftTriggertAction failed %d",result);
    }

    if((leftselectValue.isActive == XR_TRUE) && (leftselectValue.currentState == XR_TRUE))
    {
        if(!mLeftSelectPressed)
        {
            LOGI("UpdateInput mLeftSelectPressed");
            mLeftSelectPressed = true;
        }
    }
    else
    {
        if(mLeftSelectPressed)
        {
            LOGI("UpdateInput mLeftSelect up");
            mLeftSelectPressed = false;
        }
    }
    XrActionStateGetInfo getleftyInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,mLeftYAction,0};
    XrActionStateBoolean leftyValue{XR_TYPE_ACTION_STATE_BOOLEAN};
    result = xrGetActionStateBoolean(mXrSession,&getleftyInfo,&leftyValue);
    if(result != XR_SUCCESS)
    {
        LOGI("UpdateInput xrGetActionStateBoolean mLeftYAction failed %d",result);
    }

    if((leftyValue.isActive == XR_TRUE) && (leftyValue.currentState == XR_TRUE))
    {
        if(!mLeftYPressed)
        {
            LOGI("UpdateInput mLeftYPressed");
            mLeftYPressed = true;
        }
    }
    else
    {
        if(mLeftYPressed)
        {
            LOGI("UpdateInput mLeftY up");
            mLeftYPressed = false;
        }
    }

    XrActionStateGetInfo getrightInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,mRightSelectAction,0};
    XrActionStateBoolean rightselectValue{XR_TYPE_ACTION_STATE_BOOLEAN};
    result = xrGetActionStateBoolean(mXrSession,&getrightInfo,&rightselectValue);
    if(result != XR_SUCCESS)
    {
        LOGI("UpdateInput xrGetActionStateBoolean mLeftTriggertAction failed %d",result);
    }

    if((rightselectValue.isActive == XR_TRUE) && (rightselectValue.currentState == XR_TRUE))
    {
       if(!mRightSelectPressed)
        {
            LOGI("UpdateInput mRightSelectPressed");
            mRightSelectPressed = true;
        }
    }
    else
    {
        if(mRightSelectPressed)
        {
            LOGI("UpdateInput mRightSelect up");
            mRightSelectPressed = false;
        }
    }
    XrActionStateGetInfo getrightbInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,mRightBAction,0};
    XrActionStateBoolean rightbValue{XR_TYPE_ACTION_STATE_BOOLEAN};
    result = xrGetActionStateBoolean(mXrSession,&getrightbInfo,&rightbValue);
    if(result != XR_SUCCESS)
    {
        LOGI("UpdateInput xrGetActionStateBoolean mRightBAction failed %d",result);
    }

    if((rightbValue.isActive == XR_TRUE) && (rightbValue.currentState == XR_TRUE))
    {
        if(!mRightBPressed)
        {
            LOGI("UpdateInput mRightBPressed");
            mRightBPressed = true;
        }
    }
    else
    {
        if(mRightBPressed)
        {
            LOGI("UpdateInput mRightB up");
            mRightBPressed = false;
        }
    }
    //Trigger
    XrActionStateGetInfo getlefttriggerInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,mLeftTriggerAction,0};
    XrActionStateBoolean lefttriggerValue{XR_TYPE_ACTION_STATE_BOOLEAN};
    result = xrGetActionStateBoolean(mXrSession,&getlefttriggerInfo,&lefttriggerValue);
    if(result != XR_SUCCESS)
    {
        LOGI("UpdateInput xrGetActionStateBoolean mLeftTriggertAction failed %d",result);
    }

    if((lefttriggerValue.isActive == XR_TRUE) && (lefttriggerValue.currentState == XR_TRUE))
    {
        if(!mLeftTriggerPressed)
        {
            LOGI("UpdateInput mLeftTriggerPressed");
            mLeftTriggerPressed = true;
            needUpdateleftTrigger = true;
        }
    }else
    {
        if(mLeftTriggerPressed)
        {
            LOGI("UpdateInput mLeftTrigger up");
            mLeftTriggerPressed = false;
            needUpdateleftTrigger = true;
        }
    }
    XrActionStateGetInfo getrighttriggerInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,mRightTriggerAction,0};
    XrActionStateBoolean righttriggerValue{XR_TYPE_ACTION_STATE_BOOLEAN};
    result = xrGetActionStateBoolean(mXrSession,&getrighttriggerInfo,&righttriggerValue);
    if(result != XR_SUCCESS)
    {
        LOGI("UpdateInput xrGetActionStateBoolean mLeftTriggertAction failed %d",result);
    }
    if((righttriggerValue.isActive == XR_TRUE) && (righttriggerValue.currentState == XR_TRUE))
    {
        if(!mRightTriggerPressed)
        {
            LOGI("UpdateInput mRightTriggerPressed");
            mRightTriggerPressed = true;
            needUpdaterightTrigger = true;
            //SetHapticFeedback(1,0.2,3000,0.5);
        }
    }
    else
    {
        if(mRightTriggerPressed)
        {
            LOGI("UpdateInput mRightTrigger up");
            mRightTriggerPressed = false;
            needUpdaterightTrigger = true;
        }
    }
    //Grip
    XrActionStateGetInfo getleftgripInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,mLeftGripAction,0};
    XrActionStateBoolean leftgripValue{XR_TYPE_ACTION_STATE_BOOLEAN};
    result = xrGetActionStateBoolean(mXrSession,&getleftgripInfo,&leftgripValue);
    if(result != XR_SUCCESS)
    {
        LOGI("UpdateInput xrGetActionStateBoolean mLeftGripAction failed %d",result);
    }
    if((leftgripValue.isActive == XR_TRUE) && (leftgripValue.currentState == XR_TRUE))
    {
        if(!mLeftGripPressed)
        {
            LOGI("UpdateInput mLeftGripPressed");
            mLeftGripPressed = true;
        }
    }else
    {
        if(mLeftGripPressed)
        {
            LOGI("UpdateInput mLeftGrip up");
            mLeftGripPressed = false;
        }
    }
    XrActionStateGetInfo getrightgripInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,mRightGripAction,0};
    XrActionStateBoolean rightgripValue{XR_TYPE_ACTION_STATE_BOOLEAN};
    result = xrGetActionStateBoolean(mXrSession,&getrightgripInfo,&rightgripValue);
    if(result != XR_SUCCESS)
    {
        LOGI("UpdateInput xrGetActionStateBoolean mLeftTriggertAction failed %d",result);
    }

    if((rightgripValue.isActive == XR_TRUE) && (rightgripValue.currentState == XR_TRUE))
    {
        if(!mRightGripPressed)
        {
            LOGI("UpdateInput mRightGripPressed");
            mRightGripPressed = true;
        }
    }
    else
    {
        if(mRightGripPressed)
        {
            LOGI("UpdateInput mRightGrip up*");
            mRightGripPressed = false;
        }
    }
    //joystatic
    XrActionStateGetInfo getleftjoystaticInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,mLeftjoystaticAction,0};
    XrActionStateVector2f leftjoystaticValue{XR_TYPE_ACTION_STATE_VECTOR2F};
    result = xrGetActionStateVector2f(mXrSession,&getleftjoystaticInfo,&leftjoystaticValue);
    if(result != XR_SUCCESS)
    {
        LOGI("UpdateInput xrGetActionStateBoolean mLeftTriggertAction failed %d",result);
    }

    if(leftjoystaticValue.isActive == XR_TRUE)
    {
        if(leftjoystaticValue.currentState.x + leftjoystaticValue.currentState.y != 0)
        {
            //LOGI("UpdateInput mLeftjoystatic:(x:%f,y:%f)",leftjoystaticValue.currentState.x,leftjoystaticValue.currentState.y);
            mLeftjoystaticX=leftjoystaticValue.currentState.x;
            mLeftjoystaticY=leftjoystaticValue.currentState.y;
            if(!mLeftJoyStaticPressed)
            {
                mLeftJoyStaticPressed = true;
                needUpdateleftJoyStatic = true;
            }
        }
        else
        {
            mLeftjoystaticX = 0;
            mLeftjoystaticY = 0;
            if(mLeftJoyStaticPressed)
            {
                mLeftJoyStaticPressed = false;
                needUpdateleftJoyStatic = true;
            }

            //LOGI("UpdateInput mLeftjoystatic 0");
        }
    }
    XrActionStateGetInfo getrightjoystaticInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,mRightjoystaticAction,0};
    XrActionStateVector2f rightjoystaticValue{XR_TYPE_ACTION_STATE_VECTOR2F};
    result = xrGetActionStateVector2f(mXrSession,&getrightjoystaticInfo,&rightjoystaticValue);
    if(result != XR_SUCCESS)
    {
        LOGI("UpdateInput xrGetActionStateBoolean mLeftTriggertAction failed %d",result);
    }

    if(rightjoystaticValue.isActive == XR_TRUE)
    {
        if(rightjoystaticValue.currentState.x + rightjoystaticValue.currentState.y != 0)
        {
            //LOGI("UpdateInput Rightjoystatic:(x:%f,y:%f)",rightjoystaticValue.currentState.x,rightjoystaticValue.currentState.y);
            mRightjoystaticX=rightjoystaticValue.currentState.x;
            mRightjoystaticY=rightjoystaticValue.currentState.y;
            if(!mRightJoyStaticPressed)
            {
                mRightJoyStaticPressed = true;
                needUpdaterightJoyStatic = true;
            }
        }
        else
        {
            mRightjoystaticX=0;
            mRightjoystaticY=0;
            if(mRightJoyStaticPressed)
            {
                mRightJoyStaticPressed = false;
                needUpdaterightJoyStatic = true;
            }
            //LOGI("UpdateInput mRightjoystatic 0");
        }
    }
    for(int j=0; j<2; j++)
    {
        if(HapticFeedbackInfo[j].open)
        {
            LOGI("UpdateInput HapticFeedback lr:%d, amplitude:%f,time:%f,frequency%f",
                 j,HapticFeedbackInfo[j].amplitude,HapticFeedbackInfo[j].time,HapticFeedbackInfo[j].frequency);
            XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
            vibration.amplitude = HapticFeedbackInfo[j].amplitude; //振幅
            vibration.duration = HapticFeedbackInfo[j].time * 1000000000; //持续时间 纳秒
            vibration.frequency = HapticFeedbackInfo[j].frequency; //震动频率
            XrHapticActionInfo hapticActionInfo{XR_TYPE_HAPTIC_ACTION_INFO};
            hapticActionInfo.action = j==1?mRighthapticsAction:mLefthapticsAction;
            xrApplyHapticFeedback(mXrSession, &hapticActionInfo, (const XrHapticBaseHeader*)&vibration);
            HapticFeedbackInfo[j].open = false;
        }
    }

    for(int i = 0; i < 2; ++i)
    {
        XrActionStateGetInfo getInfo1{XR_TYPE_ACTION_STATE_GET_INFO};
        getInfo1.action = mPoseAction;
        getInfo1.subactionPath = mSubactionPath[i];
        XrActionStatePose statePose {XR_TYPE_ACTION_STATE_POSE};
        result =  xrGetActionStatePose(mXrSession,&getInfo1,&statePose);
        mControllerActive[i] = statePose.isActive;
//        LOGI("mControllerActive(%d)  %d",i,statePose.isActive);
        if(result != XR_SUCCESS)
        {
            LOGI("xrGetActionStatePose failed %d",result);
        }
    }

    XrSpaceLocation spaceLocation{XR_TYPE_SPACE_LOCATION};
    uint64_t connectMask = 0;
    result = xrLocateSpace(mControllerSpace[0],mBaseSpace,atTime,&spaceLocation);
    if(result != XR_SUCCESS)
    {
        LOGI("xrLocateSpace failed %d",result);
    }
    if ((spaceLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
        (spaceLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0)
    {
        mControllerPose[0] = spaceLocation.pose;
        mControllerPoseIsVaild[0] = true;
        connectMask |= 1 << 0;
    }
    else
    {
        mControllerPoseIsVaild[0] = false;
    }

    result = xrLocateSpace(mControllerSpace[1],mBaseSpace,atTime,&spaceLocation);
    if(result != XR_SUCCESS)
    {
        LOGI("xrLocateSpace failed %d",result);
    }

    if ((spaceLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
        (spaceLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0)
    {
        mControllerPose[1] = spaceLocation.pose;
        mControllerPoseIsVaild[1] = true;
        connectMask |= 1 << 1;
    }
    else
    {
        mControllerPoseIsVaild[1] = false;
    }
    if(preConnectMask != connectMask){
        preConnectMask = connectMask;
        LOGI("connectMask:%lu",connectMask);
        __system_property_set("sxr.handshank.connect.state",std::to_string(connectMask).c_str());
    }
//    LOGI("right controller:PoseIsVaild:%d,%d",mControllerPoseIsVaild[1],mControllerActive[1]);
    //LOGI("right controller position (x:%f,y:%f,z:%f) ",spaceLocation.pose.position.x,spaceLocation.pose.position.y,spaceLocation.pose.position.z);

    //Hand Aim
    {
        bool HandActive[2];
        for(int i = 0; i < 2; ++i)
        {
            XrActionStateGetInfo getInfo1{XR_TYPE_ACTION_STATE_GET_INFO};
            getInfo1.action = mAimPoseAction;
            getInfo1.subactionPath = mSubactionPath[i];
            XrActionStatePose statePose {XR_TYPE_ACTION_STATE_POSE};
            result =  xrGetActionStatePose(mXrSession,&getInfo1,&statePose);
//            LOGI("mAimPoseAction  %d",statePose.isActive);
            HandActive[i] = statePose.isActive;
            if(result != XR_SUCCESS)
            {
                LOGI("xrGetActionStatePose failed %d",result);
            }
        }
        if(HandActive[0]) {
            result = xrLocateSpace(mHandAimSpace[0], mBaseSpace, atTime,
                                   &spaceLocation);
            if (result != XR_SUCCESS) {
                LOGI("xrLocateSpace failed %d", result);
            }
            if ((spaceLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
                (spaceLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0) {
//            mControllerPose[0] = spaceLocation.pose;
//            mControllerPoseIsVaild[0] = true;
                mAimPose[0] = spaceLocation.pose;
            } else {
//            mControllerPoseIsVaild[0] = false;
            }
        }
        if(HandActive[1]) {
            result = xrLocateSpace(mHandAimSpace[1], mBaseSpace, atTime,
                                   &spaceLocation);
            if (result != XR_SUCCESS) {
                LOGI("xrLocateSpace failed %d", result);
            }

            if ((spaceLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
                (spaceLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0) {
//            mControllerPose[1] = spaceLocation.pose;
//            mControllerPoseIsVaild[1] = true;
                mAimPose[1] = spaceLocation.pose;
//                LOGI("xrLocateSpace right hand:(%f,%f,%f),(%f,%f,%f,%f)",
//                     spaceLocation.pose.position.x, spaceLocation.pose.position.y,
//                     spaceLocation.pose.position.z,
//                     spaceLocation.pose.orientation.x, spaceLocation.pose.orientation.y,
//                     spaceLocation.pose.orientation.z, spaceLocation.pose.orientation.w);
            } else {
//            mControllerPoseIsVaild[1] = false;
            }
        }
    }
}

void Input::UpdateHand(XrTime atTime)
{

}

void Input::Destroy() {
    xrDestroySpace(mControllerSpace[0]);
    xrDestroySpace(mControllerSpace[1]);
    xrDestroyActionSet(mActionSet);

}

void Input::Init(XrInstance instance, XrSession session,XrSpace space) {
    LOGI("input init");
    mXrInstance = instance;
    mXrSession = session;
    mBaseSpace = space;

    xrStringToPath(instance,"/user/hand/left",&mSubactionPath[0]);
    xrStringToPath(instance,"/user/hand/right",&mSubactionPath[1]);


    LeftHandLocations = {XR_TYPE_HAND_JOINT_LOCATIONS_EXT, nullptr, XR_FALSE, XR_HAND_JOINT_COUNT_EXT, LeftHandJointLocations};
    RightHandLocations = {XR_TYPE_HAND_JOINT_LOCATIONS_EXT, nullptr, XR_FALSE, XR_HAND_JOINT_COUNT_EXT, RightHandJointLocations};

    LeftHandGestureData = {XR_HAND_GESTURE_TYPE_UNKNOWN_QCOM,0.0,0.0};
    RightHandGestureData = {XR_HAND_GESTURE_TYPE_UNKNOWN_QCOM,0.0,0.0};

    XrResult result;
    //action set
    XrActionSetCreateInfo actionSetCreateInfo;
    strcpy(actionSetCreateInfo.actionSetName,"boundary");
    strcpy(actionSetCreateInfo.localizedActionSetName,"Boundary");
    actionSetCreateInfo.type = XR_TYPE_ACTION_SET_CREATE_INFO;
    actionSetCreateInfo.priority = 0;
    result = xrCreateActionSet(instance,&actionSetCreateInfo,&mActionSet);
    if(result != XR_SUCCESS)
    {
        LOGI("xrCreateActionSet failed %d ",result);
    }

    //pose action
    XrActionCreateInfo actionCreateInfo{XR_TYPE_ACTION_CREATE_INFO};
    actionCreateInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
    strcpy(actionCreateInfo.actionName,"controller_pose");
    strcpy(actionCreateInfo.localizedActionName,"controller pose");
    actionCreateInfo.countSubactionPaths = 2;
    actionCreateInfo.subactionPaths = mSubactionPath;
    result = xrCreateAction(mActionSet,&actionCreateInfo,&mPoseAction);
    if(result != XR_SUCCESS)
    {
        LOGI("xrCreateAction failed %d",result);
    }

    //boundary shortcut action, bind to khr simple controller "left menu" button by default
    actionCreateInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    strcpy(actionCreateInfo.actionName,"boundary_shortcut");
    strcpy(actionCreateInfo.localizedActionName,"boundary shortcut");
    actionCreateInfo.countSubactionPaths = 0;
    actionCreateInfo.subactionPaths = nullptr;
    result = xrCreateAction(mActionSet,&actionCreateInfo,&mShortcutAction);
    if(result != XR_SUCCESS)
    {
        LOGI("xrCreateAction mShortcutAction failed %d",result);
    }
    //right menu
    actionCreateInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    strcpy(actionCreateInfo.actionName,"boundary_rightsystem");
    strcpy(actionCreateInfo.localizedActionName,"boundary rightsystem");
    actionCreateInfo.countSubactionPaths = 0;
    actionCreateInfo.subactionPaths = nullptr;
    result = xrCreateAction(mActionSet,&actionCreateInfo,&mRightSystemAction);
    if(result != XR_SUCCESS)
    {
        LOGI("xrCreateAction mRightSystemAction failed %d",result);
    }
    actionCreateInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    strcpy(actionCreateInfo.actionName,"boundary_lefttrigger");
    strcpy(actionCreateInfo.localizedActionName,"boundary lefttrigger");
    actionCreateInfo.countSubactionPaths = 0;
    actionCreateInfo.subactionPaths = nullptr;
    result = xrCreateAction(mActionSet,&actionCreateInfo,&mLeftTriggerAction);
    if(result != XR_SUCCESS)
    {
        LOGI("xrCreateAction  mLeftTriggerAction failed %d",result);
    }
    actionCreateInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    strcpy(actionCreateInfo.actionName,"boundary_righttrigger");
    strcpy(actionCreateInfo.localizedActionName,"boundary righttrigger");
    actionCreateInfo.countSubactionPaths = 0;
    actionCreateInfo.subactionPaths = nullptr;
    result = xrCreateAction(mActionSet,&actionCreateInfo,&mRightTriggerAction);
    if(result != XR_SUCCESS)
    {
        LOGI("xrCreateAction mRightTriggerAction failed %d",result);
    }
    actionCreateInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    strcpy(actionCreateInfo.actionName,"boundary_leftgrip");
    strcpy(actionCreateInfo.localizedActionName,"boundary leftgrip");
    actionCreateInfo.countSubactionPaths = 0;
    actionCreateInfo.subactionPaths = nullptr;
    result = xrCreateAction(mActionSet,&actionCreateInfo,&mLeftGripAction);
    if(result != XR_SUCCESS)
    {
        LOGI("xrCreateAction  mLeftGripAction failed %d",result);
    }
    actionCreateInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    strcpy(actionCreateInfo.actionName,"boundary_rightgrip");
    strcpy(actionCreateInfo.localizedActionName,"boundary rightgrip");
    actionCreateInfo.countSubactionPaths = 0;
    actionCreateInfo.subactionPaths = nullptr;
    result = xrCreateAction(mActionSet,&actionCreateInfo,&mRightGripAction);
    if(result != XR_SUCCESS)
    {
        LOGI("xrCreateAction mRightGripAction failed %d",result);
    }
    actionCreateInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    strcpy(actionCreateInfo.actionName,"boundary_leftselect");
    strcpy(actionCreateInfo.localizedActionName,"boundary leftselect");
    actionCreateInfo.countSubactionPaths = 0;
    actionCreateInfo.subactionPaths = nullptr;
    result = xrCreateAction(mActionSet,&actionCreateInfo,&mLeftSelectAction);
    if(result != XR_SUCCESS)
    {
        LOGI("xrCreateAction  mLeftSelectAction failed %d",result);
    }
    actionCreateInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    strcpy(actionCreateInfo.actionName,"boundary_lefty");
    strcpy(actionCreateInfo.localizedActionName,"boundary lefty");
    actionCreateInfo.countSubactionPaths = 0;
    actionCreateInfo.subactionPaths = nullptr;
    result = xrCreateAction(mActionSet,&actionCreateInfo,&mLeftYAction);
    if(result != XR_SUCCESS)
    {
        LOGI("xrCreateAction  mLeftYAction failed %d",result);
    }
    actionCreateInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    strcpy(actionCreateInfo.actionName,"boundary_rightselect");
    strcpy(actionCreateInfo.localizedActionName,"boundary rightselect");
    actionCreateInfo.countSubactionPaths = 0;
    actionCreateInfo.subactionPaths = nullptr;
    result = xrCreateAction(mActionSet,&actionCreateInfo,&mRightSelectAction);
    if(result != XR_SUCCESS)
    {
        LOGI("xrCreateAction mRightSelectAction failed %d",result);
    }
    actionCreateInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    strcpy(actionCreateInfo.actionName,"boundary_rightb");
    strcpy(actionCreateInfo.localizedActionName,"boundary rightb");
    actionCreateInfo.countSubactionPaths = 0;
    actionCreateInfo.subactionPaths = nullptr;
    result = xrCreateAction(mActionSet,&actionCreateInfo,&mRightBAction);
    if(result != XR_SUCCESS)
    {
        LOGI("xrCreateAction mRightSelectAction failed %d",result);
    }
    actionCreateInfo.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
    strcpy(actionCreateInfo.actionName,"boundary_leftjoystatic");
    strcpy(actionCreateInfo.localizedActionName,"boundary leftjoystatic");
    actionCreateInfo.countSubactionPaths = 0;
    actionCreateInfo.subactionPaths = nullptr;
    result = xrCreateAction(mActionSet,&actionCreateInfo,&mLeftjoystaticAction);
    if(result != XR_SUCCESS)
    {
        LOGI("xrCreateAction mLeftjoystaticAction failed %d",result);
    }
    actionCreateInfo.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
    strcpy(actionCreateInfo.actionName,"boundary_rightjoystatic");
    strcpy(actionCreateInfo.localizedActionName,"boundary rightjoystatic");
    actionCreateInfo.countSubactionPaths = 0;
    actionCreateInfo.subactionPaths = nullptr;
    result = xrCreateAction(mActionSet,&actionCreateInfo,&mRightjoystaticAction);
    if(result != XR_SUCCESS)
    {
        LOGI("xrCreateAction mRightjoystaticXAction failed %d",result);
    }
    // create a "player_hit" output action
    XrActionCreateInfo lefthapticsactioninfo{XR_TYPE_ACTION_CREATE_INFO};
    strcpy(lefthapticsactioninfo.actionName, "left_player_hit");
    lefthapticsactioninfo.actionType = XR_ACTION_TYPE_VIBRATION_OUTPUT;
    strcpy(lefthapticsactioninfo.localizedActionName, "leftPlayer hit");
    result = xrCreateAction(mActionSet, &lefthapticsactioninfo, &mLefthapticsAction);
    if(result != XR_SUCCESS)
    {
        LOGI("xrCreateAction lefthapticsactioninfo failed %d",result);
    }
    // create a "player_hit" output action
    XrActionCreateInfo righthapticsactioninfo{XR_TYPE_ACTION_CREATE_INFO};
    strcpy(righthapticsactioninfo.actionName, "right_player_hit");
    righthapticsactioninfo.actionType = XR_ACTION_TYPE_VIBRATION_OUTPUT;
    strcpy(righthapticsactioninfo.localizedActionName, "rightPlayer hit");
    xrCreateAction(mActionSet, &righthapticsactioninfo, &mRighthapticsAction);
    if(result != XR_SUCCESS)
    {
        LOGI("xrCreateAction righthapticsactioninfo failed %d",result);
    }

    //Hand Interaction
    {
        actionCreateInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
        strcpy(actionCreateInfo.actionName,"hand_aim_pose");
        strcpy(actionCreateInfo.localizedActionName,"hand aim pose");
        actionCreateInfo.countSubactionPaths = 2;
        actionCreateInfo.subactionPaths = mSubactionPath;
        result = xrCreateAction(mActionSet,&actionCreateInfo,&mAimPoseAction);
        if(result != XR_SUCCESS)
        {
            LOGI("xrCreateAction failed %d",result);
        }

        actionCreateInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
        strcpy(actionCreateInfo.actionName,"hand_grip_pose");
        strcpy(actionCreateInfo.localizedActionName,"hand grip pose");
        actionCreateInfo.countSubactionPaths = 2;
        actionCreateInfo.subactionPaths = mSubactionPath;
        result = xrCreateAction(mActionSet,&actionCreateInfo,&mGripPoseAction);
        if(result != XR_SUCCESS)
        {
            LOGI("xrCreateAction failed %d",result);
        }
    }
    //bindings
    XrPath posePath[2], shortcutPath ,leftselectPath,rightselectPath;
    XrPath  lefttriggerPath , righttriggerPath, rightsystemPath;
    XrPath  leftyPath , rightbPath, leftgrip, rightgrip;
    XrPath  leftjoystatic , rightjoystatic;
    XrPath lefthapticPath, righthapticPath;
//    xrStringToPath(instance,"/user/hand/left/input/grip/pose",&posePath[0]);
//    xrStringToPath(instance,"/user/hand/right/input/grip/pose",&posePath[1]);

    xrStringToPath(instance,"/user/hand/left/input/aim/pose",&posePath[0]);
    xrStringToPath(instance,"/user/hand/right/input/aim/pose",&posePath[1]);

    xrStringToPath(instance,"/user/hand/left/input/x/click",&leftselectPath);
    xrStringToPath(instance,"/user/hand/left/input/y/click",&leftyPath);

    xrStringToPath(instance,"/user/hand/right/input/a/click",&rightselectPath);
    xrStringToPath(instance,"/user/hand/right/input/b/click",&rightbPath);

    xrStringToPath(instance,"/user/hand/left/input/trigger/value",&lefttriggerPath);
    xrStringToPath(instance,"/user/hand/right/input/trigger/value",&righttriggerPath);

    xrStringToPath(instance,"/user/hand/left/input/menu/click",&shortcutPath);
    xrStringToPath(instance,"/user/hand/right/input/system/click",&rightsystemPath);

    xrStringToPath(instance,"/user/hand/left/input/squeeze/value",&leftgrip);
    xrStringToPath(instance,"/user/hand/right/input/squeeze/value",&rightgrip);

    xrStringToPath(instance,"/user/hand/left/input/thumbstick",&leftjoystatic);
    xrStringToPath(instance,"/user/hand/right/input/thumbstick",&rightjoystatic);

    xrStringToPath(instance, "/user/hand/left/output/haptic", &lefthapticPath);
    xrStringToPath(instance, "/user/hand/right/output/haptic", &righthapticPath);

    XrPath khrSimpleProfilePath;
    //xrStringToPath(instance,"/interaction_profiles/khr/simple_controller",&khrSimpleProfilePath);
    //interaction_profiles/oculus/touch_controller
    xrStringToPath(instance,"/interaction_profiles/oculus/touch_controller",&khrSimpleProfilePath);
    std::vector<XrActionSuggestedBinding> bindings{{
                                                           {mPoseAction,posePath[0]},
                                                           {mPoseAction,posePath[1]},

                                                           {mShortcutAction,shortcutPath},
                                                           {mRightSystemAction,rightsystemPath},

                                                           {mLeftSelectAction,leftselectPath},
                                                           {mLeftYAction,leftyPath},

                                                           {mRightSelectAction,rightselectPath},
                                                           {mRightBAction,rightbPath},

                                                           {mLeftTriggerAction,lefttriggerPath},
                                                           {mRightTriggerAction,righttriggerPath},

                                                           {mLeftGripAction,leftgrip},
                                                           {mRightGripAction,rightgrip},

                                                           {mLeftjoystaticAction,leftjoystatic},
                                                           {mRightjoystaticAction,rightjoystatic},

                                                           {mLefthapticsAction,lefthapticPath},
                                                           {mRighthapticsAction,righthapticPath}
                                                   }};
    XrInteractionProfileSuggestedBinding suggestedBinding{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggestedBinding.interactionProfile = khrSimpleProfilePath;
    suggestedBinding.countSuggestedBindings = (uint32_t)bindings.size();
    suggestedBinding.suggestedBindings = bindings.data();
    result = xrSuggestInteractionProfileBindings(instance,&suggestedBinding);
    if(result != XR_SUCCESS)
    {
        LOGI("xrSuggestInteractionProfileBindings failed %d",result);
    }
    //手势绑定
    {
        XrPath aimposePath[2];
        xrStringToPath(instance,"/user/hand/left/input/aim/pose",&aimposePath[0]);
        xrStringToPath(instance,"/user/hand/right/input/aim/pose",&aimposePath[1]);
        std::vector<XrActionSuggestedBinding> bindingsHand{{
                                                                   {mAimPoseAction,aimposePath[0]},
                                                                   {mAimPoseAction,aimposePath[1]},

                                                                   {mGripPoseAction,posePath[0]},
                                                                   {mGripPoseAction,posePath[1]},
                                                           }};
        XrPath microsoftHandProfilePath;
        xrStringToPath(instance,"/interaction_profiles/ext/hand_interaction_ext",&microsoftHandProfilePath);
        XrInteractionProfileSuggestedBinding handsuggestedBinding{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        handsuggestedBinding.interactionProfile = microsoftHandProfilePath;
        handsuggestedBinding.countSuggestedBindings = (uint32_t)bindingsHand.size();
        handsuggestedBinding.suggestedBindings = bindingsHand.data();
        result = xrSuggestInteractionProfileBindings(instance,&handsuggestedBinding);
        if(result != XR_SUCCESS)
        {
            LOGI("xrSuggestInteractionProfileBindings microsoft hand_interaction failed %d",result);
        }

        //action spaces
        XrActionSpaceCreateInfo actionSpaceCreateInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        actionSpaceCreateInfo.action = mAimPoseAction;
        actionSpaceCreateInfo.poseInActionSpace.orientation.w = 1.0f;
        actionSpaceCreateInfo.subactionPath = mSubactionPath[0];
        xrCreateActionSpace(session,&actionSpaceCreateInfo,&mHandAimSpace[0]);
        actionSpaceCreateInfo.subactionPath = mSubactionPath[1];
        result = xrCreateActionSpace(session,&actionSpaceCreateInfo,&mHandAimSpace[1]);
        if(result != XR_SUCCESS)
        {
            LOGI("xrCreateActionSpace handaim failed %d",result);
        }
    }

    //action spaces
    XrActionSpaceCreateInfo actionSpaceCreateInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
    actionSpaceCreateInfo.action = mPoseAction;
    actionSpaceCreateInfo.poseInActionSpace.orientation.w = 1.0f;
    actionSpaceCreateInfo.subactionPath = mSubactionPath[0];
    xrCreateActionSpace(session,&actionSpaceCreateInfo,&mControllerSpace[0]);
    actionSpaceCreateInfo.subactionPath = mSubactionPath[1];
    result = xrCreateActionSpace(session,&actionSpaceCreateInfo,&mControllerSpace[1]);
    if(result != XR_SUCCESS)
    {
        LOGI("xrCreateActionSpace failed %d",result);
    }

    XrSessionActionSetsAttachInfo attachInfo{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attachInfo.countActionSets = 1;
    attachInfo.actionSets = &mActionSet;
    result = xrAttachSessionActionSets(session,&attachInfo);
    if(result != XR_SUCCESS)
    {
        LOGI("xrAttachSessionActionSets failed %d",result);
    }

}

void Input::SetSelectClickCallback(std::function<void (int lr)>  fun)
{
    mSelectCallback = fun;
}

void Input::SetHapticFeedback(struct HapticFeedbackInfo info)
{
    LOGI("Input::SetHapticFeedback failed %d",info.lr);
    if(info.lr==0)
    {
        HapticFeedbackInfo[0].lr = 0;
        HapticFeedbackInfo[0].time= info.time;
        HapticFeedbackInfo[0].frequency=info.frequency;
        HapticFeedbackInfo[0].amplitude=info.amplitude;
        HapticFeedbackInfo[0].open = true;
    }
    else if(info.lr==1)
    {
        HapticFeedbackInfo[1].lr = 1;
        HapticFeedbackInfo[1].time = info.time;
        HapticFeedbackInfo[1].frequency=info.frequency;
        HapticFeedbackInfo[1].amplitude=info.amplitude;
        HapticFeedbackInfo[1].open = true;
    }
}
