#include "mophunmod/target.h"

namespace mophunmod {

TargetProfile makeVrally2Rc14EuM5Target()
{
	TargetFingerprint fingerprint;
	fingerprint.heapSize = 0;
	fingerprint.stackSize = 128;
	fingerprint.flags = 0;
	fingerprint.codeSize = 0x82f0;
	fingerprint.dataSize = 0x0a44;
	fingerprint.bssSize = 0xcdd0;
	fingerprint.resourceSize = 0xd1e8;
	fingerprint.directorySize = 0;
	fingerprint.poolSize = 0x1d2;
	fingerprint.stringSize = 0x25b;

	const uint32_t carUpdateOffset = 0x3dc4;
	return TargetProfile("vrally2-rc14eu-m5", "V-Rally 2 RC14EU multiscreen M5",
		fingerprint, {
			// Mophun OS entry points are discovered by their imported names, so their
			// pool ordering is not baked into the Turbo patcher.
			TargetSymbol::importedPool("os.graphics.draw_flat_polygon", "vDrawFlatPolygon"),
			TargetSymbol::importedPool("os.graphics.fill_rect", "vFillRect"),
			TargetSymbol::importedPool("os.graphics.flip_screen", "vFlipScreen"),
			TargetSymbol::importedPool("os.input.get_button_data", "vGetButtonData"),
			TargetSymbol::importedPool("os.time.get_tick_count", "vGetTickCount"),
			TargetSymbol::importedPool("os.graphics.set_clip_window", "vSetClipWindow"),
			TargetSymbol::importedPool("os.graphics.set_fore_color", "vSetForeColor"),

			// V-Rally internals have no import names, so the profile gives them stable
			// semantic names and validates their expected entry/signature.
			TargetSymbol::fixedPool("game.car.update", 185,
				PoolEntry::code(carUpdateOffset)),
			TargetSymbol::codeOffset("game.car.update.code", carUpdateOffset,
				{0x43, 0x08, 0x18, 0x00, 0x11, 0x0c, 0x30, 0x00}),
			TargetSymbol::constant("game.car.started_offset", 0x00),
			TargetSymbol::constant("game.car.target_speed_offset", 0x24),
			TargetSymbol::constant("game.car.speed_offset", 0x28),
			TargetSymbol::constant("game.car.jump_height_offset", 0xac),
			TargetSymbol::constant("game.race_update_hz", 15)
		});
}

} // namespace mophunmod
