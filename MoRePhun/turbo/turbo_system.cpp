#include "turbo_system.h"
#include "../mophun_vm.h"
#include "../syscall/graphics.h"
#include "../syscall/input_facilities.h"
#include "../video.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>

namespace {

constexpr float GameFrameSeconds = 0.066f; // V-Rally 2 paces itself at 66 ms per frame
constexpr int MeterX = 95;
constexpr int MeterY = 138;
constexpr int MeterWidth = 32;
constexpr int MeterHeight = 7;

const char* stateName(int state)
{
	static const char* names[] = {"NORMAL", "READY", "CINEMATIC", "ACTIVE", "EXPIRING"};
	return names[state];
}

bool fileExists(const std::string& path)
{
	std::ifstream input(path, std::ios::binary);
	return input.good();
}

// 3x5 glyphs for the debug overlay (digits, letters, a few symbols).
const uint8_t* glyph(char c)
{
	static const uint8_t digits[10][5] = {
		{7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},
		{7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7}};
	static const uint8_t letters[26][5] = {
		{7,5,7,5,5},{6,5,6,5,6},{7,4,4,4,7},{6,5,5,5,6},{7,4,7,4,7},{7,4,7,4,4},{7,4,5,5,7},
		{5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,7},{5,5,6,5,5},{4,4,4,4,7},{5,7,7,5,5},{6,5,5,5,5},
		{7,5,5,5,7},{7,5,7,4,4},{7,5,5,7,1},{7,5,6,5,5},{7,4,7,1,7},{7,2,2,2,2},{5,5,5,5,7},
		{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},{5,5,7,2,2},{7,1,2,4,7}};
	static const uint8_t blank[5] = {0,0,0,0,0};
	static const uint8_t dot[5] = {0,0,0,0,2};
	static const uint8_t percent[5] = {5,1,2,4,5};
	static const uint8_t colon[5] = {0,2,0,2,0};
	static const uint8_t dash[5] = {0,0,7,0,0};
	if (c >= '0' && c <= '9') return digits[c - '0'];
	if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
	if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
	switch (c)
	{
		case '.': return dot;
		case '%': return percent;
		case ':': return colon;
		case '-': return dash;
		default: return blank;
	}
}

} // namespace

TurboSystem::TurboSystem(MophunVM& vm, Video& video, const TurboConfig& config)
	: config(config), vm(vm), video(video), probe(vm, config.carStructAddress), audio(this->config),
	cinematic(this->config, video.app.renderer, SCREEN_WIDTH, SCREEN_HEIGHT)
{
	debugOverlay = config.debugOverlay;
	lastRealSeconds = now();
	cinematic.setSfxHandler([this](TurboSfx effect, float gain) { audio.playSfx(effect, gain); });
	cinematic.setLoopHandler([this](TurboLoop loop, float level) { audio.setLoop(loop, level); });
	for (int i = 0; i < 10; ++i)
	{
		streakPhase[i] = randomUnit();
		// Streaks fan out toward the lower and side edges only.
		const float spread = 0.25f + 0.5f * randomUnit();
		streakAngle[i] = (i % 2 == 0 ? 1.0f : -1.0f) * spread * 3.14159f + 3.14159f * 0.5f;
	}
	if (config.devInstantCharge)
		charge = config.maxCharge;
}

TurboSystem::~TurboSystem()
{
}

double TurboSystem::now() const
{
	return static_cast<double>(SDL_GetPerformanceCounter()) / static_cast<double>(SDL_GetPerformanceFrequency());
}

float TurboSystem::randomUnit()
{
	effectNoise = effectNoise * 1664525U + 1013904223U;
	return static_cast<float>(effectNoise >> 8) / 16777216.0f;
}

void TurboSystem::setAssetDirectory(const std::string& directory)
{
	assetDirectory = directory;
	ensureMusic();
}

void TurboSystem::ensureMusic()
{
	if (musicSearched || audio.musicLoaded())
		return;
	musicSearched = true;
	std::vector<std::string> candidates;
	if (!config.musicPath.empty())
		candidates.push_back(config.musicPath);
	if (!assetDirectory.empty())
		candidates.push_back(assetDirectory + "/turbo_music.mp3");
	candidates.push_back("turbo_music.mp3");
	candidates.push_back("assets/turbo_music.mp3"); // repository checkout, run from its root
	if (char* base = SDL_GetBasePath())
	{
		candidates.push_back(std::string(base) + "turbo_music.mp3");
		candidates.push_back(std::string(base) + "../assets/turbo_music.mp3"); // dist/ launcher
		SDL_free(base);
	}
	for (const std::string& candidate : candidates)
	{
		if (!fileExists(candidate))
			continue;
		std::string error;
		if (audio.loadMusic(candidate, error))
		{
			std::cout << "Turbo music: " << candidate << std::endl;
			return;
		}
		std::cerr << "Turbo music could not be decoded (" << candidate << "): " << error << std::endl;
	}
	std::cout << "Turbo music not found (place turbo_music.mp3 next to the game files or set MOPHUN_TURBO_MUSIC); "
		"the cinematic will run on a silent clock." << std::endl;
}

// --- Hooks --------------------------------------------------------------------------------

void TurboSystem::onFillRect()
{
	++fillRectsThisFrame;
}

void TurboSystem::onDrawObject(int x, int y, const SPRITE* sprite)
{
	if (sprite == nullptr)
		return;
	// The player car is the large sprite anchored at the chase camera's fixed
	// bottom-centre point (64,147); roadside objects can be large too but never
	// share that anchor.
	if (sprite->width >= 24 && sprite->height >= 20 && x >= 60 && x <= 68 && y >= 140 && y <= 154)
	{
		frameCarRect = {x - sprite->centerx, y - sprite->centery, sprite->width, sprite->height};
		frameCarRectSeen = true;
	}
}

uint32_t TurboSystem::filterInput(uint32_t keys)
{
	rawKeys = keys;
	if (state == State::Cinematic)
	{
		lastKeys = 0;
		return 0;
	}
	const bool upNow = (keys & KEY_UP) != 0;
	const bool upBefore = (lastKeys & KEY_UP) != 0;
	if (!upNow)
		upReleasedSinceReady = true;
	if (state == State::Ready && inRace && upNow && !upBefore && upReleasedSinceReady
		&& charge >= config.minimumActivationCharge)
		pendingActivation = true;

	if ((state == State::Active || state == State::Expiring) && inRace && lastPokeFrame != frameIndex)
	{
		// The game has just computed this frame's target speed from throttle,
		// steering and surface; scale it before the physics step consumes it.
		// The game's target speed is stateful (it carries our previous poke
		// minus drag/steering), so cap the boost at an absolute value instead of
		// compounding the multiplier frame after frame.
		const CarSnapshot live = probe.read();
		if (live.targetSpeed > 0)
		{
			const double boosted = std::min(static_cast<double>(live.targetSpeed) * boostMultiplier(),
				static_cast<double>(config.speedReference) * boostMultiplier());
			probe.writeTargetSpeed(static_cast<int32_t>(std::max(static_cast<double>(live.targetSpeed), boosted)));
		}
		lastPokeFrame = frameIndex;
	}
	lastKeys = keys;
	return keys;
}

uint32_t TurboSystem::guestTicks()
{
	const double real = now();
	virtualMs += (real - lastRealSeconds) * 1000.0 * timeScale;
	lastRealSeconds = real;
	pump();
	return static_cast<uint32_t>(virtualMs);
}

void TurboSystem::onFlip(const char* screenshotPath)
{
	++frameIndex;
	previousCar = car;
	car = probe.read();
	const bool wasInRace = inRace;
	inRace = car.started && fillRectsThisFrame >= config.roadBandsPerRaceFrame;
	fillRectsThisFrame = 0;
	if (frameCarRectSeen)
	{
		carRect = frameCarRect;
		haveCarRect = true;
	}
	frameCarRectSeen = false;

	if (!car.started && state != State::Cinematic)
		resetForNewRace();
	if (inRace && !wasInRace && !previousCar.started)
		resetForNewRace();

	if (config.devTriggerFrame != 0 && frameIndex == config.devTriggerFrame)
	{
		charge = config.maxCharge;
		pendingActivation = true;
	}

	if ((state == State::Normal || state == State::Ready) && inRace)
		updateMeter(GameFrameSeconds);

	if (pendingActivation && inRace && (state == State::Normal || state == State::Ready))
	{
		pendingActivation = false;
		if (config.skipCinematic)
		{
			charge = 0.0f;
			displayCharge = 0.0f;
			video.captureSnapshot();
			samplePalette();
			endCinematic();
		}
		else
			startCinematic();
	}
	pendingActivation = false;

	if ((state == State::Active || state == State::Expiring) && inRace)
	{
		turboElapsed += GameFrameSeconds;
		if (state == State::Active && turboElapsed >= config.turboDuration)
			enterState(State::Expiring);
		else if (state == State::Expiring && turboElapsed >= config.turboDuration + config.turboExpireDuration)
			enterState(State::Normal);
	}

	if (state == State::Cinematic)
	{
		video.captureSnapshot();
		renderCinematicFrame(screenshotPath);
		return;
	}

	if (inRace)
		drawHud();
	if (debugOverlay)
		drawDebugOverlay();
	video.captureSnapshot();
	if ((state == State::Active || state == State::Expiring) && inRace)
		presentComposite(screenshotPath, true);
	else
		video.present(screenshotPath);
	lastPresentSeconds = now();
}

// --- State ---------------------------------------------------------------------------------

void TurboSystem::resetForNewRace()
{
	if (state == State::Cinematic)
		return;
	if (state != State::Normal)
		enterState(State::Normal);
	charge = config.devInstantCharge ? config.maxCharge : 0.0f;
	displayCharge = charge;
	turboElapsed = 0.0f;
	pendingActivation = false;
	particles.clear();
}

void TurboSystem::enterState(State next)
{
	if (state == next)
		return;
	state = next;
	switch (next)
	{
		case State::Normal:
			audio.setLoop(TurboLoop::TurboEngine, 0.0f);
			audio.setLoop(TurboLoop::AirShockwave, 0.0f);
			audio.setLoop(TurboLoop::EngineRumble, 0.0f);
			timeScale = 1.0;
			effectIntensity = 0.0f;
			break;
		case State::Ready:
			upReleasedSinceReady = (lastKeys & KEY_UP) == 0;
			audio.playSfx(TurboSfx::Thump, 0.6f);
			audio.playSfx(TurboSfx::CoverClick, 0.35f);
			break;
		case State::Cinematic:
			break;
		case State::Active:
			timeScale = 1.0;
			turboElapsed = 0.0f;
			activeStartSeconds = now();
			effectIntensity = 1.0f;
			audio.setLoop(TurboLoop::EngineRumble, 0.0f);
			audio.setLoop(TurboLoop::TurboEngine, 0.85f);
			audio.setLoop(TurboLoop::AirShockwave, 0.45f);
			audio.playSfx(TurboSfx::Ignition, 1.0f);
			audio.playSfx(TurboSfx::Thump, 1.0f);
			break;
		case State::Expiring:
			audio.fadeOutMusic(config.turboExpireDuration + config.musicFadeOutDuration);
			audio.setLoop(TurboLoop::TurboEngine, 0.4f);
			audio.setLoop(TurboLoop::AirShockwave, 0.15f);
			break;
	}
}

float TurboSystem::boostMultiplier() const
{
	if (state == State::Active)
		return config.turboSpeedMultiplier;
	if (state == State::Expiring)
	{
		const float t = (turboElapsed - config.turboDuration) / std::max(0.05f, config.turboExpireDuration);
		return 1.0f + (config.turboSpeedMultiplier - 1.0f) * (1.0f - std::max(0.0f, std::min(1.0f, t)));
	}
	return 1.0f;
}

void TurboSystem::updateMeter(float dt)
{
	speedRatio = car.speed / std::max(1.0f, config.speedReference);
	braking = (rawKeys & KEY_DOWN) != 0;
	reversing = car.speed < 0;
	offroad = !braking && car.targetSpeed > 0
		&& static_cast<uint32_t>(car.targetSpeed) < config.offroadTargetThreshold;

	if (collisionTimer > 0.0f)
		collisionTimer -= dt;
	collisionFlash = std::max(0.0f, collisionFlash - dt * 3.0f);
	const float drop = static_cast<float>(previousCar.speed - car.speed);
	if (previousCar.started && !braking && previousCar.speed > 0
		&& drop > config.collisionSpeedDropRatio * config.speedReference && collisionTimer <= 0.0f)
	{
		charge -= config.collisionPenalty;
		collisionTimer = config.collisionCooldown;
		collisionFlash = 1.0f;
		audio.playSfx(TurboSfx::Thump, 0.8f);
	}

	float rate = 0.0f;
	if (reversing)
		rate = -config.reverseDecayRate;
	else if (braking)
		rate = -config.brakeDecayRate;
	else if (offroad)
		rate = -config.offroadDecayRate;
	else if (speedRatio < config.lowSpeedThreshold)
		rate = -config.decayRateLow;
	else if (speedRatio < config.highSpeedThreshold)
		rate = 0.0f;
	else if (speedRatio < config.maxSpeedThreshold)
		rate = config.chargeRateHigh;
	else
	{
		const float t = (speedRatio - config.maxSpeedThreshold) / std::max(0.01f, 1.0f - config.maxSpeedThreshold);
		rate = config.chargeRateHigh + (config.chargeRateMax - config.chargeRateHigh) * std::min(1.0f, t);
	}
	if (config.devInstantCharge && rate < 0.0f)
		rate = 0.0f;
	charge = std::max(0.0f, std::min(config.maxCharge, charge + rate * dt));
	displayCharge += (charge - displayCharge) * std::min(1.0f, config.meterSmoothing * dt);

	if (state == State::Normal && charge >= config.minimumActivationCharge)
		enterState(State::Ready);
	else if (state == State::Ready && charge < config.minimumActivationCharge)
		enterState(State::Normal);
}

void TurboSystem::samplePalette()
{
	SDL_Texture* snapshot = video.app.snapshot;
	if (snapshot == nullptr)
		return;
	std::vector<uint8_t> pixels(SCREEN_WIDTH * SCREEN_HEIGHT * 4);
	video.beginHostTarget(snapshot);
	const int result = SDL_RenderReadPixels(video.app.renderer, nullptr, SDL_PIXELFORMAT_RGBA32,
		pixels.data(), SCREEN_WIDTH * 4);
	video.restoreGuestTarget();
	if (result != 0)
		return;
	auto pixel = [&](int x, int y) {
		const uint8_t* p = pixels.data() + (y * SCREEN_WIDTH + x) * 4;
		return Rgb(p[0], p[1], p[2]);
	};
	auto dominant = [&](int x0, int x1, int y0, int y1) {
		std::map<uint32_t, int> counts;
		for (int y = y0; y < y1; ++y)
			for (int x = x0; x < x1; ++x)
			{
				const Rgb q = quantizeRgb332(pixel(x, y));
				counts[(q.r << 16) | (q.g << 8) | q.b]++;
			}
		uint32_t best = 0;
		int bestCount = -1;
		for (const auto& entry : counts)
			if (entry.second > bestCount)
			{
				best = entry.first;
				bestCount = entry.second;
			}
		return Rgb(static_cast<uint8_t>(best >> 16), static_cast<uint8_t>(best >> 8), static_cast<uint8_t>(best));
	};
	// Sample away from the HUD (timer top-left, direction sign top-centre,
	// gauges along the bottom) and from both road shoulders.
	auto same = [](const Rgb& a, const Rgb& b) { return a.r == b.r && a.g == b.g && a.b == b.b; };
	CinematicPalette sampled;
	sampled.sky = dominant(84, 124, 20, 34);
	sampled.horizon = mixRgb(sampled.sky, Rgb(255, 255, 255), 0.35f);
	sampled.mountain = dominant(30, 98, 48, 62);
	const Rgb groundA = dominant(2, 16, 118, 130);
	const Rgb groundB = dominant(2, 16, 96, 108);
	sampled.ground = groundA;
	sampled.groundAlt = same(groundA, groundB) ? scaleRgb(groundA, 0.82f) : groundB;
	// The road is whatever dominates just ahead of the car that is not ground;
	// off-road at activation keeps the default grey.
	const Rgb ahead = dominant(40, 88, 96, 114);
	const int saturation = std::max(ahead.r, std::max(ahead.g, ahead.b)) - std::min(ahead.r, std::min(ahead.g, ahead.b));
	if (!same(ahead, groundA) && !same(ahead, groundB) && saturation < 48)
		sampled.road = ahead;
	sampled.roadAlt = scaleRgb(sampled.road, 1.1f);
	palette = sampled;
	if (debugOverlay)
		std::cout << "Turbo palette: sky " << int(palette.sky.r) << "," << int(palette.sky.g) << "," << int(palette.sky.b)
			<< " ground " << int(palette.ground.r) << "," << int(palette.ground.g) << "," << int(palette.ground.b)
			<< " alt " << int(palette.groundAlt.r) << "," << int(palette.groundAlt.g) << "," << int(palette.groundAlt.b)
			<< " road " << int(palette.road.r) << "," << int(palette.road.g) << "," << int(palette.road.b) << std::endl;
}

void TurboSystem::startCinematic()
{
	ensureMusic();
	charge = 0.0f;
	displayCharge = 0.0f;
	video.captureSnapshot();
	samplePalette();
	cinematic.begin(palette, haveCarRect ? carRect : SDL_Rect{46, 115, 36, 36});
	audio.startMusic();
	audio.setLoop(TurboLoop::EngineRumble, 0.2f);
	cinematicStartSeconds = now();
	lastCinematicTime = 0.0;
	cinematicFramesRendered = 0;
	skipRequested = false;
	timeScale = config.activationTimeScale;
	enterState(State::Cinematic);
	std::cout << "Turbo cinematic started (music " << config.musicStartOffset << "s -> drop "
		<< config.musicDropOffset << "s)" << std::endl;
}

void TurboSystem::renderCinematicFrame(const char* screenshotPath)
{
	double t = audio.musicPosition() - config.musicStartOffset;
	if (debugOverlay && cinematicFramesRendered % 120 == 0)
		std::cout << "cinematic clock: position=" << audio.musicPosition() << " t=" << t << " paced=" << audio.deviceAvailable() << std::endl;
	if (t < 0.0)
		t = 0.0;
	if (t < lastCinematicTime)
		t = lastCinematicTime;
	if (skipRequested)
		t = cinematic.dropTime();
	lastCinematicTime = t;

	video.beginHostTarget(video.app.composite);
	cinematicFrame = cinematic.render(t, video.app.snapshot);
	if (debugOverlay)
		drawDebugOverlay();
	video.restoreGuestTarget();
	timeScale = cinematicFrame.gameTimeScale;

	std::string devPath;
	const char* path = screenshotPath;
	if (!config.devShotDirectory.empty() && cinematicFramesRendered % std::max(1U, config.devShotEvery) == 0)
	{
		char name[64];
		std::snprintf(name, sizeof(name), "/shot_%05u.bmp", cinematicFramesRendered);
		devPath = config.devShotDirectory + name;
		path = devPath.c_str();
	}
	++cinematicFramesRendered;
	video.present(path, video.app.composite);
	lastPresentSeconds = now();

	if (cinematicFrame.finished)
		endCinematic();
}

void TurboSystem::endCinematic()
{
	// The drop: gameplay camera, control, boost and music release together.
	enterState(State::Active);
	lastKeys = rawKeys;
	upReleasedSinceReady = false;
	// Let the guest's frame limiter expire immediately so the first boosted
	// frame is drawn without waiting out the remainder of a 66 ms period.
	virtualMs += 70.0;
	std::cout << "Turbo active: handoff at music position " << audio.musicPosition()
		<< "s (drop configured at " << config.musicDropOffset << "s)" << std::endl;
}

// --- Per-tick pump ------------------------------------------------------------------------

void TurboSystem::pump()
{
	const double real = now();
	if (real - lastDevPollSeconds >= 0.016)
	{
		lastDevPollSeconds = real;
		pollDevKeys();
	}
	if (state == State::Cinematic)
	{
		if (real - lastPresentSeconds >= 1.0 / std::max(15.0f, config.shotFrameRate))
			renderCinematicFrame(nullptr);
		return;
	}
	if ((state == State::Active || state == State::Expiring) && inRace)
	{
		if (real - lastPresentSeconds >= 1.0 / 60.0)
		{
			presentComposite(nullptr, true);
			lastPresentSeconds = real;
		}
	}
}

void TurboSystem::pollDevKeys()
{
	const Uint8* keys = SDL_GetKeyboardState(nullptr);
	auto edge = [&](int index, SDL_Scancode code) {
		const bool down = keys[code] != 0;
		const bool pressed = down && !devKeyState[index];
		devKeyState[index] = down;
		return pressed;
	};
	if (edge(0, SDL_SCANCODE_F1))
	{
		charge = config.maxCharge;
		displayCharge = charge;
	}
	if (edge(1, SDL_SCANCODE_F2) && (state == State::Normal || state == State::Ready))
	{
		charge = config.maxCharge;
		pendingActivation = true;
	}
	if (edge(2, SDL_SCANCODE_F3) && state == State::Cinematic)
		skipRequested = true;
	if (edge(3, SDL_SCANCODE_F4))
		debugOverlay = !debugOverlay;
	if (edge(4, SDL_SCANCODE_F5))
	{
		if (state == State::Cinematic)
		{
			audio.stopMusic();
			enterState(State::Normal);
			startCinematic();
		}
		else if (state == State::Normal || state == State::Ready)
		{
			charge = config.maxCharge;
			pendingActivation = true;
		}
	}
	if (edge(5, SDL_SCANCODE_F6))
		config.musicDropOffset -= 0.05f;
	if (edge(6, SDL_SCANCODE_F7))
		config.musicDropOffset += 0.05f;
	if (edge(7, SDL_SCANCODE_F8))
		config.cinematicTimeScale = std::max(0.01f, config.cinematicTimeScale * 0.8f);
	if (edge(8, SDL_SCANCODE_F9))
		config.cinematicTimeScale = std::min(1.0f, config.cinematicTimeScale * 1.25f);
	if (edge(9, SDL_SCANCODE_F10))
		config.musicStartOffset -= 0.25f;
	if (edge(10, SDL_SCANCODE_F11))
		config.musicStartOffset += 0.25f;
}

// --- Drawing ----------------------------------------------------------------------------------

void TurboSystem::drawHud()
{
	SDL_Renderer* renderer = video.app.renderer;
	video.beginHostTarget(video.app.framebuffer);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
	const double seconds = now();
	const float pulse = 0.5f + 0.5f * static_cast<float>(std::sin(seconds * 9.0));

	Rgb border(0, 0, 0);
	Rgb background(28, 28, 44);
	Rgb fill(72, 160, 255);
	float fraction = displayCharge / config.maxCharge;
	if (state == State::Ready)
	{
		fill = mixRgb(Rgb(255, 255, 255), Rgb(255, 224, 72), pulse);
		border = mixRgb(Rgb(0, 0, 0), Rgb(255, 224, 72), pulse * 0.8f);
	}
	else if (state == State::Active || state == State::Expiring)
	{
		fraction = 1.0f - std::min(1.0f, turboElapsed / (config.turboDuration + config.turboExpireDuration));
		fill = Rgb(255, 140, 32);
	}
	if (collisionFlash > 0.0f)
		background = mixRgb(background, Rgb(200, 40, 40), collisionFlash);

	SDL_Rect outer = {MeterX, MeterY, MeterWidth, MeterHeight};
	SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, 255);
	SDL_RenderFillRect(renderer, &outer);
	SDL_Rect inner = {MeterX + 1, MeterY + 1, MeterWidth - 2, MeterHeight - 2};
	SDL_SetRenderDrawColor(renderer, background.r, background.g, background.b, 255);
	SDL_RenderFillRect(renderer, &inner);
	// Ten 2-pixel segments with 1-pixel gaps, like the game's own gauge.
	const int segments = 10;
	const float filled = fraction * segments;
	SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, 255);
	for (int segment = 0; segment < segments; ++segment)
	{
		const float portion = std::max(0.0f, std::min(1.0f, filled - segment));
		if (portion <= 0.0f)
			break;
		SDL_Rect bar = {MeterX + 1 + segment * 3, MeterY + 1, portion >= 0.5f ? 2 : 1, MeterHeight - 2};
		if (portion < 0.5f)
			SDL_SetRenderDrawColor(renderer, fill.r / 2, fill.g / 2, fill.b / 2, 255);
		SDL_RenderFillRect(renderer, &bar);
		SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, 255);
	}
	video.restoreGuestTarget();
}

void TurboSystem::updateParticles(float dt)
{
	if (!haveCarRect)
		return;
	const Rgb dustColor = mixRgb(palette.ground, Rgb(255, 240, 210), 0.35f);
	const int spawn = state == State::Active ? 3 : 1;
	for (int i = 0; i < spawn; ++i)
	{
		for (float side : {-1.0f, 1.0f})
		{
			Particle particle;
			particle.x = carRect.x + carRect.w * 0.5f + side * (carRect.w * 0.36f + randomUnit() * 3.0f);
			particle.y = carRect.y + carRect.h - 4.0f + randomUnit() * 3.0f;
			particle.vx = side * (14.0f + randomUnit() * 26.0f);
			particle.vy = 26.0f + randomUnit() * 40.0f;
			particle.maxLife = 0.35f + randomUnit() * 0.35f;
			particle.life = particle.maxLife;
			particle.size = 1.0f + randomUnit() * 2.2f;
			particle.color = randomUnit() < 0.3f ? Rgb(255, 170, 60) : dustColor;
			particles.push_back(particle);
		}
	}
	for (Particle& particle : particles)
	{
		particle.x += particle.vx * dt;
		particle.y += particle.vy * dt;
		particle.vy += 30.0f * dt;
		particle.life -= dt;
	}
	particles.erase(std::remove_if(particles.begin(), particles.end(),
		[](const Particle& p) { return p.life <= 0.0f || p.y > SCREEN_HEIGHT + 4; }), particles.end());
	if (particles.size() > 160)
		particles.erase(particles.begin(), particles.begin() + (particles.size() - 160));
}

void TurboSystem::drawEffects(float intensity, double seconds)
{
	SDL_Renderer* renderer = video.app.renderer;
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	const float t = static_cast<float>(seconds);
	const float flicker = 0.5f + 0.5f * std::sin(t * 41.0f) * std::sin(t * 23.0f + 1.0f);

	// Speed streaks radiating from the vanishing point toward the periphery.
	const float vanishX = SCREEN_WIDTH * 0.5f;
	const float vanishY = 68.0f;
	for (int i = 0; i < 10; ++i)
	{
		const float speed = 1.4f + (i % 3) * 0.5f;
		const float s = std::fmod(streakPhase[i] + t * speed, 1.0f);
		if (s < 0.45f)
			continue;
		const float radius = 120.0f;
		const float dx = std::cos(streakAngle[i]);
		const float dy = std::sin(streakAngle[i]);
		const float alpha = std::min(1.0f, (s - 0.45f) * 2.5f) * intensity;
		SDL_SetRenderDrawColor(renderer, 255, 255, 255, static_cast<Uint8>(150 * alpha));
		SDL_RenderDrawLine(renderer, static_cast<int>(vanishX + dx * radius * s), static_cast<int>(vanishY + dy * radius * s),
			static_cast<int>(vanishX + dx * radius * (s + 0.12f)), static_cast<int>(vanishY + dy * radius * (s + 0.12f)));
	}

	// Dust particles.
	for (const Particle& particle : particles)
	{
		const float fade = particle.life / particle.maxLife;
		SDL_SetRenderDrawColor(renderer, particle.color.r, particle.color.g, particle.color.b,
			static_cast<Uint8>(220 * fade * intensity));
		SDL_Rect rect = {static_cast<int>(particle.x), static_cast<int>(particle.y),
			static_cast<int>(std::ceil(particle.size)), static_cast<int>(std::ceil(particle.size))};
		SDL_RenderFillRect(renderer, &rect);
	}

	if (!haveCarRect)
		return;
	const float exhaustX = carRect.x + carRect.w * 0.36f;
	const float exhaustY = carRect.y + carRect.h - 4.0f;

	// Exhaust trail: translucent column that fades toward the bottom edge.
	for (int i = 0; i < 7; ++i)
	{
		const float fade = (1.0f - i / 7.0f) * intensity;
		SDL_SetRenderDrawColor(renderer, 255, 150, 60, static_cast<Uint8>(70 * fade));
		SDL_Rect rect = {static_cast<int>(exhaustX - 3 - i * 0.5f), static_cast<int>(exhaustY + 8 + i * 3),
			6 + i, 3};
		SDL_RenderFillRect(renderer, &rect);
	}

	// Flame: three nested fans pointing back toward the camera (down-screen).
	const float length = (9.0f + 7.0f * flicker) * (0.6f + 0.4f * intensity);
	const float jitter = std::sin(t * 57.0f) * 2.0f;
	const Rgb layers[3] = {Rgb(220, 40, 20), Rgb(255, 140, 20), Rgb(255, 240, 130)};
	const float widths[3] = {8.0f, 5.5f, 3.0f};
	const float lengths[3] = {1.0f, 0.72f, 0.42f};
	for (int layer = 0; layer < 3; ++layer)
	{
		const SDL_Color color = {layers[layer].r, layers[layer].g, layers[layer].b, static_cast<Uint8>(235 * intensity)};
		SDL_Vertex vertices[3];
		vertices[0].position = {exhaustX - widths[layer] * 0.5f, exhaustY};
		vertices[1].position = {exhaustX + widths[layer] * 0.5f, exhaustY};
		vertices[2].position = {exhaustX + jitter * lengths[layer], exhaustY + length * lengths[layer]};
		for (SDL_Vertex& vertex : vertices)
		{
			vertex.color = color;
			vertex.tex_coord = {0, 0};
		}
		SDL_RenderGeometry(renderer, nullptr, vertices, 3, nullptr, 0);
		// A smaller side tongue for a broader, rougher flame.
		vertices[2].position = {exhaustX - widths[layer] * 0.8f + jitter * 0.5f, exhaustY + length * lengths[layer] * 0.55f};
		SDL_RenderGeometry(renderer, nullptr, vertices, 3, nullptr, 0);
	}
	// Sparks.
	for (int i = 0; i < 4; ++i)
	{
		const float phase = std::fmod(t * (3.0f + i) + i * 0.37f, 1.0f);
		SDL_SetRenderDrawColor(renderer, 255, 230, 120, static_cast<Uint8>(200 * (1.0f - phase) * intensity));
		SDL_RenderDrawPoint(renderer, static_cast<int>(exhaustX + std::sin(i * 2.0f + t * 9.0f) * 6.0f),
			static_cast<int>(exhaustY + 4.0f + phase * 22.0f));
	}

	// Air dome: layered arcs of compressed air ahead of the car.
	const float domeCx = carRect.x + carRect.w * 0.5f;
	const float domeCy = carRect.y - 6.0f;
	const float shimmer = 1.0f + 0.03f * std::sin(t * 9.0f);
	for (int ring = 0; ring < 3; ++ring)
	{
		const float rx = (carRect.w * 0.72f + ring * 5.0f) * shimmer;
		const float ry = 10.0f + ring * 3.0f;
		const Uint8 alpha = static_cast<Uint8>((120 - ring * 32) * intensity);
		SDL_SetRenderDrawColor(renderer, 205, 238, 255, alpha);
		SDL_Point points[17];
		for (int i = 0; i <= 16; ++i)
		{
			const float angle = 3.14159f + 3.14159f * i / 16.0f;
			points[i].x = static_cast<int>(std::lround(domeCx + std::cos(angle) * rx));
			points[i].y = static_cast<int>(std::lround(domeCy + std::sin(angle) * ry + ring * 1.5f));
		}
		SDL_RenderDrawLines(renderer, points, 17);
	}
	// Faint fill under the innermost arc.
	{
		SDL_Vertex fan[18];
		const SDL_Color inner = {225, 245, 255, static_cast<Uint8>(34 * intensity)};
		const SDL_Color edge = {225, 245, 255, 0};
		fan[0].position = {domeCx, domeCy + 4.0f};
		fan[0].color = inner;
		fan[0].tex_coord = {0, 0};
		for (int i = 0; i <= 16; ++i)
		{
			const float angle = 3.14159f + 3.14159f * i / 16.0f;
			fan[i + 1].position = {domeCx + std::cos(angle) * carRect.w * 0.72f * shimmer, domeCy + std::sin(angle) * 10.0f};
			fan[i + 1].color = edge;
			fan[i + 1].tex_coord = {0, 0};
		}
		int indices[48];
		for (int i = 0; i < 16; ++i)
		{
			indices[i * 3] = 0;
			indices[i * 3 + 1] = i + 1;
			indices[i * 3 + 2] = i + 2;
		}
		SDL_RenderGeometry(renderer, nullptr, fan, 18, indices, 48);
	}
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

void TurboSystem::presentComposite(const char* screenshotPath, bool effects)
{
	const double seconds = now();
	const float dt = static_cast<float>(std::max(0.0, std::min(0.1, seconds - lastPresentSeconds)));
	float intensity = 1.0f;
	if (state == State::Expiring)
		intensity = 1.0f - std::max(0.0f, std::min(1.0f, (turboElapsed - config.turboDuration) / std::max(0.05f, config.turboExpireDuration)));
	effectIntensity = intensity;
	if (effects)
		updateParticles(dt);

	SDL_Renderer* renderer = video.app.renderer;
	video.beginHostTarget(video.app.composite);
	// Subtle speed distortion: a breathing zoom and a one-pixel shudder.
	const float zoom = intensity * (1.0f + 0.5f * static_cast<float>(std::sin(seconds * 11.0)));
	const int inset = static_cast<int>(std::lround(zoom * 1.5f));
	const int shake = intensity > 0.3f && std::fmod(seconds * 30.0, 2.0) < 1.0 ? 1 : 0;
	SDL_Rect source = {inset + shake, inset, SCREEN_WIDTH - inset * 2, SCREEN_HEIGHT - inset * 2};
	SDL_SetTextureBlendMode(video.app.snapshot, SDL_BLENDMODE_NONE);
	SDL_RenderCopy(renderer, video.app.snapshot, &source, nullptr);
	if (effects)
		drawEffects(intensity, seconds);
	video.restoreGuestTarget();
	video.present(screenshotPath, video.app.composite);
}

void TurboSystem::drawText(int x, int y, const std::string& text, Rgb color)
{
	SDL_Renderer* renderer = video.app.renderer;
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_Rect backing = {x - 1, y - 1, static_cast<int>(text.size()) * 4 + 1, 7};
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
	SDL_RenderFillRect(renderer, &backing);
	SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);
	for (char c : text)
	{
		const uint8_t* rows = glyph(c);
		for (int row = 0; row < 5; ++row)
			for (int column = 0; column < 3; ++column)
				if (rows[row] & (4 >> column))
					SDL_RenderDrawPoint(renderer, x + column, y + row);
		x += 4;
	}
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

void TurboSystem::drawDebugOverlay()
{
	const bool inCinematic = state == State::Cinematic;
	if (!inCinematic)
		video.beginHostTarget(video.app.framebuffer);
	char line[64];
	std::snprintf(line, sizeof(line), "%s %3.0f%%", stateName(static_cast<int>(state)), charge);
	drawText(2, 12, line, Rgb(255, 255, 120));
	std::snprintf(line, sizeof(line), "SPD %d R%.2f", car.speed, speedRatio);
	drawText(2, 19, line, Rgb(200, 255, 200));
	std::snprintf(line, sizeof(line), "TGT %d %s%s%s", car.targetSpeed, offroad ? "OFF " : "", braking ? "BRK " : "",
		inRace ? "" : "MENU");
	drawText(2, 26, line, Rgb(200, 220, 255));
	if (haveCarRect && !inCinematic)
	{
		std::snprintf(line, sizeof(line), "CAR %d %d %dX%d", carRect.x, carRect.y, carRect.w, carRect.h);
		drawText(2, 47, line, Rgb(200, 200, 200));
	}
	if (inCinematic)
	{
		std::snprintf(line, sizeof(line), "S%d %s T%.2f", cinematicFrame.shotIndex, cinematicFrame.shotName, lastCinematicTime);
		drawText(2, 33, line, Rgb(255, 200, 255));
		std::snprintf(line, sizeof(line), "DROP %.2f TS %.2f", config.musicDropOffset, config.cinematicTimeScale);
		drawText(2, 40, line, Rgb(255, 200, 255));
	}
	else if (state == State::Active || state == State::Expiring)
	{
		std::snprintf(line, sizeof(line), "BOOST X%.2f %.1fS", boostMultiplier(), turboElapsed);
		drawText(2, 33, line, Rgb(255, 180, 120));
	}
	if (!inCinematic)
		video.restoreGuestTarget();
}
