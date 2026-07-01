-- WinAlp UI Theme Configuration
-- Loaded on startup to customize the holographic HUD colors and behavior.
-- This is a Lua script executed in a sandboxed runtime.
-- All values are optional; defaults are used if not set.

theme = {
    -- Background gradient (dark base)
    bg = { r = 8, g = 12, b = 20 },

    -- Primary accent color (listening state)
    accent = { r = 0, g = 212, b = 255 },

    -- Secondary accent (thinking state)
    accent2 = { r = 123, g = 47, b = 190 },

    -- Writing state accent
    accent3 = { r = 0, g = 200, b = 180 },

    -- Action state accent (amber)
    accent4 = { r = 245, g = 158, b = 11 },

    -- Text color
    text = { r = 200, g = 210, b = 220 },

    -- Orb pulse speed (multiplier, 1.0 = normal)
    orb_speed = 1.0,

    -- Orb size multiplier
    orb_scale = 1.0,

    -- Maximum particles (0 = use adaptive LOD default)
    max_particles = 0,
}

-- Return theme table for validation (optional)
return theme
