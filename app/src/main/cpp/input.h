//
// Created by eryazhan on 10/18/2022.
//

#ifndef BOUNDARYSERVICE_INPUT_H
#define BOUNDARYSERVICE_INPUT_H

#include "AppCommon.h"
#include "openxr_qcom.h"

#include <functional>
#include <string>
struct HapticFeedbackInfo
{
    int lr=0;
    float time = 0.2;
    float frequency=3000;
    float amplitude=0.5;
    bool open = false;
};

enum class EHandKeypoint : int
{
    Palm,
    Wrist,
    ThumbMetacarpal,
    ThumbProximal,
    ThumbDistal,
    ThumbTip,
    IndexMetacarpal,
    IndexProximal,
    IndexIntermediate,
    IndexDistal,
    IndexTip,
    MiddleMetacarpal,
    MiddleProximal,
    MiddleIntermediate,
    MiddleDistal,
    MiddleTip,
    RingMetacarpal,
    RingProximal,
    RingIntermediate,
    RingDistal,
    RingTip,
    LittleMetacarpal,
    LittleProximal,
    LittleIntermediate,
    LittleDistal,
    LittleTip
};

class Input {
public:
    Input(){
        mAimPose[0].orientation.w = 1;
        mAimPose[1].orientation.w = 1;
    }
    void Init(XrInstance instance, XrSession session,XrSpace space);
    void UpdateInput(XrTime atTime);
    void Destroy();
    void SetHapticFeedback(HapticFeedbackInfo info);
    void SetSelectClickCallback(std::function<void (int lr)> fun);
	void UpdateHand(XrTime atTime);
    bool IsControllerActive(uint32_t i){
        if(i < 2) {
            return mControllerActive[i] && mControllerPoseIsVaild[i];
        }
        return false;
    }
private:
    XrInstance mXrInstance = nullptr;
    XrSession mXrSession = nullptr;
    XrSpace mBaseSpace = nullptr;
    XrActionSet mActionSet = nullptr;

    //左手柄menu
    XrAction mShortcutAction = nullptr;
    XrAction mRightSystemAction = nullptr;

    XrAction mLeftSelectAction = nullptr;
    XrAction mLeftYAction = nullptr;

    XrAction mRightSelectAction = nullptr;
    XrAction mRightBAction = nullptr;

    XrAction mLeftTriggerAction = nullptr;
    XrAction mRightTriggerAction = nullptr;

    XrAction mLeftGripAction = nullptr;
    XrAction mRightGripAction = nullptr;

    XrAction mLeftjoystaticAction = nullptr;
    XrAction mRightjoystaticAction = nullptr;

    XrAction mPoseAction = nullptr;

    XrAction mLefthapticsAction;
    XrAction mRighthapticsAction;

    XrPath mSubactionPath[2];
    XrSpace mControllerSpace[2];

    std::string mShortcutPath;

    HapticFeedbackInfo HapticFeedbackInfo[2];
    //Hand interaction
    XrSpace mHandAimSpace[2];
    XrAction mAimPoseAction = nullptr;
    XrAction mGripPoseAction = nullptr;
//    XrAction mSelectAction = nullptr;

    uint64_t preConnectMask = 0;
public:
    XrPosef mControllerPose[2];
    XrBool32 mControllerActive[2];
    bool mControllerPoseIsVaild[2];

    //Hand
    XrPosef mAimPose[2];

    std::function<void (int lr)> mSelectCallback;

    XrHandTrackerEXT LeftHandTracker{XR_NULL_HANDLE};
    XrHandTrackerEXT RightHandTracker{XR_NULL_HANDLE};
    XrHandJointLocationEXT  LeftHandJointLocations[XR_HAND_JOINT_COUNT_EXT];
    XrHandJointLocationsEXT LeftHandLocations;
    XrHandJointLocationEXT  RightHandJointLocations[XR_HAND_JOINT_COUNT_EXT];
    XrHandJointLocationsEXT RightHandLocations;

    XrHandGestureQCOM LeftHandGestureData;
    XrHandGestureQCOM RightHandGestureData;

    //left menu
    bool mShortcutPressed = false;
    bool mRightSystemPressed = false;
    //X
    bool mLeftSelectPressed = false;
    bool mLeftYPressed = false;
    //A
    bool mRightSelectPressed = false;
    bool mRightBPressed = false;

    bool mLeftTriggerPressed = false;
    bool mRightTriggerPressed = false;

    bool mLeftJoyStaticPressed = false;
    bool mRightJoyStaticPressed = false;
    bool needUpdaterightJoyStatic = false;
    bool needUpdateleftJoyStatic = false;

    bool needUpdaterightTrigger = false;
    bool needUpdateleftTrigger = false;
    bool mHMDKeyPressed = false;
    bool needUpdateHMDKe = false;

    float mLeftGripPressed = false;
    float mRightGripPressed = false;

    float mLeftjoystaticX = 0;
    float mLeftjoystaticY = 0;
    float mRightjoystaticX = 0;
    float mRightjoystaticY = 0;

    bool mSetFloorLevelPressed = false;
    bool mSaveFloorLevelPressed = false;
};


#endif //BOUNDARYSERVICE_INPUT_H
