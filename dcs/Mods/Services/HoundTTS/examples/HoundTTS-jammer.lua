do


    function UserSpaceLogging(msg,duration)
        local duration = duration or 10
        trigger.action.outText(msg,duration)
        env.info("***** "..msg.." *****")
    end

    HoundNoise = {
        next = 1
    }
    -- One test occupies a single slot: the noise burst, then the follow-up voice
    -- call. The DLL transmissions run asynchronously, so the voice call is queued
    -- instead of being started on top of the noise, and the next test is
    -- scheduled one whole slot later so the two can never overlap.
    local NOISE_SECONDS = 10
    local VOICE_SLOT    = 6    -- time the queued voice call may use (~5 s speech)
    -- Slot = noise + 1 s tail before the voice call + the voice slot itself.
    -- Both delays below derive from these values, so they cannot drift apart.
    local TEST_SLOT     = NOISE_SECONDS + 1 + VOICE_SLOT

    function HoundNoise.run(self)
        local tests = {
            {name = "white/pink noise", profile = "pink"},
            {name = "chirp", profile = "chirp"},
            {name = "harsh", profile = "harsh"},
            {name = "jam", profile = "jam"}
        }
        if self.next > #tests then
            UserSpaceLogging("Finished all noise transmissions.",10)
            return nil
        end
        local currentTest = tests[self.next]
        UserSpaceLogging(string.format("Transmitting Noise (%s) (%d/%d)",currentTest.name,self.next,#tests),TEST_SLOT)
        self.noiseMaker(currentTest.profile, NOISE_SECONDS)
        -- Next test starts one full slot after this one began.
        local next_test_time = timer.getTime() + TEST_SLOT
        UserSpaceLogging(string.format("Finished %s transmission.",currentTest.name),2)
        self.next = self.next + 1
        return next_test_time + 2
    end

    function HoundNoise.noiseMaker(profile, duration)
        local duration = duration or NOISE_SECONDS
        local transmitter_params = {
            freqs = "251.0",
            coalition = 0,
        }
        local noiseSessionId = HoundTTS.TransmitNoise(
            transmitter_params,
            {
                noiseType = profile,
                duration = duration
            }
        )
        if noiseSessionId == nil then
            UserSpaceLogging(string.format("Failed to start %s noise transmission.", profile), 2)
        end
        -- Serialize: queue the voice call for after the noise has finished
        -- (plus the DLL's 200 ms tail padding) so the two never overlap.
        timer.scheduleFunction(function()
            local ok, err = HoundTTS.Transmit(
                string.format("Radio call with %s transmission. Can you hear me?", profile),
                transmitter_params
            )
            if not ok then
                env.error("[HoundTTS] jammer voice call failed: " .. tostring(err))
            end
            return nil
        end, nil, timer.getTime() + duration + 1)
    end

    timer.scheduleFunction(HoundNoise.run, HoundNoise, timer.getTime() + 2)
end