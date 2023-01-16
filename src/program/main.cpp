#include "lib.hpp"
#include "patches.hpp"
#include "nn/err.h"
#include "logger/Logger.hpp"
#include "fs.h"

#include <basis/seadRawPrint.h>
#include <prim/seadSafeString.h>
#include <resource/seadResourceMgr.h>
#include <filedevice/nin/seadNinSDFileDeviceNin.h>
#include <filedevice/seadFileDeviceMgr.h>
#include <filedevice/seadPath.h>
#include <resource/seadArchiveRes.h>
#include <framework/seadFramework.h>
#include <heap/seadHeapMgr.h>
#include <heap/seadExpHeap.h>
#include <devenv/seadDebugFontMgrNvn.h>
#include <gfx/seadTextWriter.h>
#include <gfx/seadViewport.h>

#include "al/util.hpp"
#include "game/StageScene/StageScene.h"
#include "game/System/GameSystem.h"
#include "game/System/Application.h"
#include "game/HakoniwaSequence/HakoniwaSequence.h"
#include "rs/util.hpp"

#include "al/util.hpp"
#include "al/fs/FileLoader.h"
#include "al/util/LiveActorUtil.h"
#include "al/camera/CameraTargetBase.h"
#include "al/actor/ActorSceneInfo.h"
#include <thread>
#include <threads.h>
#include "al/util/LiveActorUtil.h"
#include "al/util/RandomUtil.h"

static const char *DBG_FONT_PATH   = "DebugData/Font/nvn_font_jis1.ntx";
static const char *DBG_SHADER_PATH = "DebugData/Font/nvn_font_shader_jis1.bin";
static const char *DBG_TBL_PATH    = "DebugData/Font/nvn_font_jis1_tbl.bin";

sead::TextWriter *gTextWriter;
bool isInStage = false;
void graNoclipCode(al::LiveActor *player) {

    static bool isFirst = true;

    float speed = 25.0f;
    float speedMax = 150.0f;
    float vspeed = 20.0f;
    float speedGain = 0.0f;

    sead::Vector3f *playerPos = al::getTransPtr(player);
    sead::Vector3f *cameraPos = al::getCameraPos(player, 0);
    sead::Vector2f *leftStick = al::getLeftStick(-1);

    // Its better to do this here because loading zones reset this.
    al::offCollide(player);
    al::setVelocityZero(player);

    // Mario slightly goes down even when velocity is 0. This is a hacky fix for that.
    playerPos->y += 1.4553f;

    float d = sqrt(al::powerIn(playerPos->x - cameraPos->x, 2) + (al::powerIn(playerPos->z - cameraPos->z, 2)));
    float vx = ((speed + speedGain)/d)*(playerPos->x - cameraPos->x);
    float vz = ((speed + speedGain)/d)*(playerPos->z - cameraPos->z);

    if (!al::isPadHoldZR(-1)) {
        playerPos->x -= leftStick->x * vz;
        playerPos->z += leftStick->x * vx;

        playerPos->x += leftStick->y * vx;
        playerPos->z += leftStick->y * vz;

        if (al::isPadHoldX(-1)) speedGain -= 0.5f;
        if (al::isPadHoldY(-1)) speedGain += 0.5f;
        if (speedGain <= 0.0f) speedGain = 0.0f;
        if (speedGain >= speedMax) speedGain = speedMax;

        if (al::isPadHoldZL(-1) || al::isPadHoldA(-1)) playerPos->y -= (vspeed + speedGain/6);
        if (al::isPadHoldB(-1)) playerPos->y += (vspeed + speedGain/6);
    }
}

void controlLol(StageScene* scene) {
    auto actor = rs::getPlayerActor(scene);

    static bool isNoclip = false;

    if(al::isPadTriggerRight(-1)) {
        isNoclip = !isNoclip;

        if(!isNoclip) {
            al::onCollide(actor);
        }
    }

    if(isNoclip) {
        graNoclipCode(actor);
    }
}

HOOK_DEFINE_TRAMPOLINE(ControlHook) {
    static void Callback(StageScene* scene) {
        controlLol(scene);
        Orig(scene);
        isInStage = true;
    }
};

HOOK_DEFINE_REPLACE(ReplaceSeadPrint) {
    static void Callback(const char* format, ...) {
        va_list args;
        va_start(args, format);
        Logger::log(format, args);
        va_end(args);
    }
};

HOOK_DEFINE_TRAMPOLINE(CreateFileDeviceMgr) {
    static void Callback(sead::FileDeviceMgr *thisPtr) {
        
        Orig(thisPtr);

        thisPtr->mMountedSd = nn::fs::MountSdCardForDebug("sd").isSuccess();

        sead::NinSDFileDevice *sdFileDevice = new sead::NinSDFileDevice();

        thisPtr->mount(sdFileDevice);
    }
};

HOOK_DEFINE_TRAMPOLINE(RedirectFileDevice) {
    static sead::FileDevice *Callback(sead::FileDeviceMgr *thisPtr, sead::SafeString &path, sead::BufferedSafeString *pathNoDrive) {

        sead::FixedSafeString<32> driveName;
        sead::FileDevice* device;

        // Logger::log("Path: %s\n", path.cstr());

        if (!sead::Path::getDriveName(&driveName, path))
        {
            
            device = thisPtr->findDevice("sd");

            if(!(device && device->isExistFile(path))) {

                device = thisPtr->getDefaultFileDevice();

                if (!device)
                {
                    Logger::log("drive name not found and default file device is null\n");
                    return nullptr;
                }

            }else {
                Logger::log("Found File on SD! Path: %s\n", path.cstr());
            }
            
        }
        else
            device = thisPtr->findDevice(driveName);

        if (!device)
            return nullptr;

        if (pathNoDrive != NULL)
            sead::Path::getPathExceptDrive(pathNoDrive, path);

        return device;
    }
};

HOOK_DEFINE_TRAMPOLINE(FileLoaderLoadArc) {
    static sead::ArchiveRes *Callback(al::FileLoader *thisPtr, sead::SafeString &path, const char *ext, sead::FileDevice *device) {

        // Logger::log("Path: %s\n", path.cstr());

        sead::FileDevice* sdFileDevice = sead::FileDeviceMgr::instance()->findDevice("sd");

        if(sdFileDevice && sdFileDevice->isExistFile(path)) {

            Logger::log("Found File on SD! Path: %s\n", path.cstr());

            device = sdFileDevice;
        }

        return Orig(thisPtr, path, ext, device);
    }
};

HOOK_DEFINE_TRAMPOLINE(FileLoaderIsExistFile) {
    static bool Callback(al::FileLoader *thisPtr, sead::SafeString &path, sead::FileDevice *device) {

        sead::FileDevice* sdFileDevice = sead::FileDeviceMgr::instance()->findDevice("sd");

        if(sdFileDevice && sdFileDevice->isExistFile(path)) device = sdFileDevice;

        return Orig(thisPtr, path, device);
    }
};

HOOK_DEFINE_TRAMPOLINE(GameSystemInit) {
    static void Callback(GameSystem *thisPtr) {

        sead::Heap* curHeap = sead::HeapMgr::instance()->getCurrentHeap();

        sead::DebugFontMgrJis1Nvn::createInstance(curHeap);

        if (al::isExistFile(DBG_SHADER_PATH) && al::isExistFile(DBG_FONT_PATH) && al::isExistFile(DBG_TBL_PATH)) {
            sead::DebugFontMgrJis1Nvn::sInstance->initialize(curHeap, DBG_SHADER_PATH, DBG_FONT_PATH, DBG_TBL_PATH, 0x100000);
        }

        sead::TextWriter::setDefaultFont(sead::DebugFontMgrJis1Nvn::sInstance);

        al::GameDrawInfo* drawInfo = Application::instance()->mDrawInfo;

        agl::DrawContext *context = drawInfo->mDrawContext;
        agl::RenderBuffer* renderBuffer = drawInfo->mFirstRenderBuffer;

        sead::Viewport* viewport = new sead::Viewport(*renderBuffer);

        gTextWriter = new sead::TextWriter(context, viewport);

        gTextWriter->setupGraphics(context);

        gTextWriter->mColor = sead::Color4f(1.f, 1.f, 1.f, 0.8f);

        Orig(thisPtr);

    }
};

HOOK_DEFINE_TRAMPOLINE(OhNo) {
static void Callback(HakoniwaSequence *thisPtr) {

    Orig(thisPtr);
    if (isInStage) {
        int LastJumpHeighty = 0;
        int LastJumpHeightx = 0;
        int LastJumpHeightz = 0;
        gTextWriter->beginDraw();
        //New thread for setting a timer for 5 seconds
        gTextWriter->setCursorFromTopLeft(sead::Vector2f(10.f, 10.f));
        sead::Vector3f pos = getVelocity(rs::getPlayerActor(thisPtr->curScene));
        gTextWriter->printf("X: %f\nY: %f\nZ: %f\n", pos.x, pos.y, pos.z);
        //Every second, make there be a 1% chance of the player y velocity being set to +100
        if (al::isPadHoldB(-1)) {
            LastJumpHeighty = pos.y + al::getRandom(-23, 23);
                LastJumpHeightx = pos.x + al::getRandom(-23, 23);
                LastJumpHeightz = pos.z + al::getRandom(-23, 23);
            al::setVelocityY(rs::getPlayerActor(thisPtr->curScene), LastJumpHeighty);
            al::setVelocityZ(rs::getPlayerActor(thisPtr->curScene), LastJumpHeightz);
            al::setVelocityX(rs::getPlayerActor(thisPtr->curScene), LastJumpHeightx);

        }
        if (al::isPadHoldA(-1)) {
            LastJumpHeightx = pos.x + al::getRandom(-23, 23);
            LastJumpHeightz = pos.z + al::getRandom(-23, 23);
            LastJumpHeighty = pos.y + al::getRandom(-23, 23);
                al::setVelocityZ(rs::getPlayerActor(thisPtr->curScene), LastJumpHeightz);
                al::setVelocityX(rs::getPlayerActor(thisPtr->curScene), LastJumpHeightx);
            al::setVelocityY(rs::getPlayerActor(thisPtr->curScene), LastJumpHeighty);
            al::setVelocityZ(rs::getPlayerActor(thisPtr->curScene), LastJumpHeightz);
            al::setVelocityX(rs::getPlayerActor(thisPtr->curScene), LastJumpHeightx);

        }

        gTextWriter->endDraw();
    } else {
        gTextWriter->beginDraw();
        gTextWriter->setCursorFromTopLeft(sead::Vector2f(10.f, 10.f));
        gTextWriter->printf("FPS: %d\n", static_cast<int>(round(Application::instance()->mFramework->calcFps())));
        gTextWriter->printf("Not In A Stage! Go into a stage to begin the challenge!\n");
        gTextWriter->endDraw();
    }
}
};
extern "C" void exl_main(void* x0, void* x1) {
    /* Setup hooking enviroment. */
    envSetOwnProcessHandle(exl::util::proc_handle::Get());
    exl::hook::Initialize();


    runCodePatches();

    GameSystemInit::InstallAtOffset(0x535850);

    // SD File Redirection

    RedirectFileDevice::InstallAtOffset(0x76CFE0);
    FileLoaderLoadArc::InstallAtOffset(0xA5EF64);
    CreateFileDeviceMgr::InstallAtOffset(0x76C8D4);
    FileLoaderIsExistFile::InstallAtOffset(0xA5ED28);

    // Sead Debugging Overriding

    ReplaceSeadPrint::InstallAtOffset(0xB59E28);

    // Debug Text Writer Drawing

    OhNo::InstallAtOffset(0x50F1D8);

    ControlHook::InstallAtSymbol("_ZN10StageScene7controlEv");

}

extern "C" NORETURN void exl_exception_entry() {
    /* TODO: exception handling */
    EXL_ABORT(0x420);
}