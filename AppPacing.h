#pragma once


namespace Loom
{
	constexpr double active_wait = 0.05;
	constexpr double idle_wait = 0.5;
	constexpr double animated_wait = 0.2;
	constexpr double input_linger = 1.0;

	// Seconds to wait for events before the next frame. Any event still ends the wait at once.
	// A window on screen wakes often enough to step its sprites, a minimised one draws nothing and waits longer.
	constexpr double FrameWait(
		bool busy,
		bool held,
		double sinceInput,
		bool minimised = false)
	{
		return busy || (!minimised && (held || sinceInput < input_linger)) ? active_wait : minimised ? idle_wait : animated_wait;
	};
};
