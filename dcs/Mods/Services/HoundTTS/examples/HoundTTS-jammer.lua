do


    function UserSpaceLogging(msg,duration)
        local duration = duration or 10
        trigger.action.outText(msg,duration)
        env.info("***** "..msg.." *****")
    end

    HoundNoise = {
        next = 1
    }
    function HoundNoise.run(self)
        local tests = {
            {name = "white/pink noise", profile = "pink", delay_next_by = 10},
            {name = "chirp", profile = "chirp", delay_next_by = 10},
            {name = "harsh", profile = "harsh", delay_next_by = 10},
            {name = "jam", profile = "jam", delay_next_by = 10}
        }
        if self.next > #tests then
            UserSpaceLogging("Finished all noise transmissions.",10)
            return nil
        end
        local currentTest = tests[self.next]
        UserSpaceLogging(string.format("Transmitting Noise (%s) (%d/%d)",currentTest.name,self.next,#tests),currentTest.delay_next_by)
        self.noiseMaker(currentTest.profile, 10)
        local next_test_time = timer.getTime() + currentTest.delay_next_by
        UserSpaceLogging(string.format("Finished %s transmission.",currentTest.name),2) 
        self.next = self.next + 1
        return next_test_time + 2
    end

    function HoundNoise.noiseMaker(profile, duration)
        local duration = duration or 10
        local transmitter_params = {
            freqs = "251.0",
            coalition = 0,
        }
        HoundTTS.TransmitNoise(
            transmitter_params,
            {
                noiseType = profile,
                duration = duration
            }
        )
        HoundTTS.Transmit(
            string.format("Radio call with %s transmission. Can you hear me?", profile),
            transmitter_params
        )
    end

    timer.scheduleFunction(HoundNoise.run, HoundNoise, timer.getTime() + 2)
end