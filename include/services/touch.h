#pragma once

namespace services::touch {

/** Reset the CST816 controller and start the I2C bus. Call once in setup(). */
void init();

/**
 * Poll the controller. Returns true exactly once per new touch (the
 * 0->1 finger transition), so a single tap toggles a single action.
 */
bool tapped();

}  // namespace services::touch
