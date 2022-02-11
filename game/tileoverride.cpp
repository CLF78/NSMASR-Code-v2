#include <kamek.h>
#include <dBgActorManager.h>
#include <dRes.h>
#include <profileid.h>
#include <stdlib/new.h>
#include <stdlib/string.h>
#include "tileoverride.h"

// Extern for ASM call
extern "C" {
void DoObjOverride(dBgActorManager_c* mng, char* tileNames);
void DestroyOverrides();
}

// External data
extern char Rail, RailWhite, RailDaishizen, RailMinigame;
extern dBgActorManager_c::BgObjName_t* OriginalOverrides[5];

void ParseObjOverride(ProfsBinEntry* entries, u32 entryCount, dBgActorManager_c::BgObjName_t* buffer) {
	for (int i = 0; i < entryCount; i++) {

		// Get current entry
		ProfsBinEntry* currSrc = &entries[i];
		dBgActorManager_c::BgObjName_t* currDest = &buffer[i];

		// Copy the tile number
		currDest->tileNum = currSrc->tileNumber;

		// Copy the actor number based on version
		u16 profileId = currSrc->actorID;
		u8 profileIdVersion = currSrc->actorIDVersion;
		switch(profileIdVersion) {
			case PROFILEKOR:
				if (profileId >= 702)
					profileId += 2;
				break;
			case PROFILECHN:
				if (profileId >= 621)
					profileId += 2;
				if (profileId >= 704)
					profileId += 2;
				break;
		}
		currDest->profileId = profileId;

		// Copy the rest using memcpy
		memcpy(&currDest->xOffs, &currSrc->xOffs, 0x18);
	}
}

void DoObjOverride(dBgActorManager_c* mng, char* tileNames) {

	// Store pointers to the overrides and their lengths on the stack
	ProfsBin* files[4] = { NULL };
	u32 lengths[4] = { 0 };

	// Set up type to 0 and count to 1 (to account for dummy final entry)
	int type = 0;
	u32 count = 1;

	// Initial parsing
	for (int i = 0; i < 4; i++) {

		// Get tileset name
		const char* currTileName = &tileNames[32*i];

		// If not set, go to next tileset
		if (currTileName[0] == '\0')
			continue;

		// Get override file
		ProfsBin* currFile = (ProfsBin*)dResMng_c::instance->res.getRes(currTileName, OVERRIDEFILE, &lengths[i]);

		// Check if file was found and that the version matches
		if (currFile != NULL && currFile->version == SPECVERSION) {

			// Store it to the stack array for later
			files[i] = currFile;

			// Set number of entries
			lengths[i] -= 4;
			lengths[i] /= sizeof(ProfsBinEntry);

			// Override rail color, but only if set
			if (currFile->railColor != 0)
				type = currFile->railColor;

		// File not found or invalid, replicate donut lifts
		} else if (i == 0)
			lengths[i] = 1;

		// File not found or invalid, replicate rails
		else if (i == 3) {
			if (!strcmp(currTileName, &Rail)) {
				type = 1;
				lengths[i] = 26;
			} else if (!strcmp(currTileName, &RailWhite)) {
				type = 2;
				lengths[i] = 26;
			} else if (!strcmp(currTileName, &RailDaishizen)) {
				type = 3;
				lengths[i] = 13;
			} else if (!strcmp(currTileName, &RailMinigame)) {
				type = 4;
				lengths[i] = 19;
			} else
				lengths[i] = 0;

		// Nothing to override here...
		} else
			lengths[i] = 0;

		// Increase count for later
		count += lengths[i];
	}

	// Set type for rail colors
	mng->type = type;

	// Failsafe
	if (count == 0)
		return;

	// Allocate buffer
	dBgActorManager_c::BgObjName_t* buffer = (dBgActorManager_c::BgObjName_t*)nw(count * sizeof(dBgActorManager_c::BgObjName_t));

	// Counter to keep track of parsed entries
	int parsedCount = 0;

	// Parse the entries
	for (int i = 0; i < 4; i++) {

		// Failsafe for empty files
		if (lengths[i]) {

			// If file was found, let the parser deal with it
			if (files[i] != NULL) {
				ParseObjOverride(files[i]->entries, lengths[i], &buffer[parsedCount]);

			// Else if slot is Pa0, copy the donut lifts override
			} else if (i == 0)
				memcpy(&buffer[parsedCount], &OriginalOverrides[0][0], sizeof(dBgActorManager_c::BgObjName_t));

			// Else if slot is Pa3 and type is set, copy the corresponding rail overrides
			else if (i == 3 && type != 0)
				memcpy(&buffer[parsedCount], &OriginalOverrides[type][1], lengths[i] * sizeof(dBgActorManager_c::BgObjName_t));
			}

		// Increase counter
		parsedCount += lengths[i];

	}

	// Set dummy final entry
	buffer[parsedCount].profileId = ProfileId::DUMMY_ACTOR;

	// Store array to static address
	dBgActorManager_c::bgObjNameList = buffer;
}

void DestroyOverrides() {
	// Delete allocated overrides
	dl(&dBgActorManager_c::bgObjNameList);
}

kmBranchDefAsm(0x8007E30C, 0x8007E3AC) {
	// If there are no zones, r3 will be 0, in that case bail
	cmpwi r3, 0
	beq end

	// Setup call + modified original instruction
	lwz r4, 0x8(r3)
	mr r3, r29
	bl DoObjOverride

	// Return later to skip all the Nintendo fluff
	end:
}

kmCallDefAsm(0x8007E270) {
	// Push stack
	stwu r1, -0x10(r1)
	mflr r0
	stw r0, 0x14(r1)

	// Call destroyer
	bl DestroyOverrides

	// Original instruction
	li r0, 0

	// Pop stack and return
	lwz r12, 0x14(r1)
	mtlr r12
	addi r1, r1, 0x10
	blr
}