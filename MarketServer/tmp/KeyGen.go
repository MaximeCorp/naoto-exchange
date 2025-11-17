package main

import (
	"fmt"
	"os"
	"strconv"
	"time"

	"github.com/sony/sonyflake"
)

const timeLayout = "2006-01-02T15:04:05Z"

var (
	sf            *sonyflake.Sonyflake
	startDate   time.Time
	machineId uint16
)

func initSnowFlake() {
	getMachineID := func() (uint16, error) {
        return machineId, nil
    }

	settings := sonyflake.Settings{
		StartTime: startDate, 
		MachineID: getMachineID,
	}

	sf = sonyflake.NewSonyflake(settings)
	if sf == nil {
		panic("Sonyflake generator failed to initialize")
	}
}

func generateUniqueKey(assetID string) (string, error) {
	id, err := sf.NextID()
	if err != nil {
		return "", fmt.Errorf("failed to generate ID: %w", err)
	}

	parts := sonyflake.Decompose(id)
	
	timeUnits := parts["time"]

	sequence := parts["sequence"]

	timeMsFromEpoch := timeUnits * 10

	unixMs := startDate.UnixMilli() + int64(timeMsFromEpoch)
	
	timestampStr := strconv.FormatInt(unixMs, 10)

	sequenceStr := fmt.Sprintf("%04d", sequence) 

	key := fmt.Sprintf("%s:%s:%s", assetID, timestampStr, sequenceStr)

	return key, nil
}

func init() {
	machineIdStr := os.Getenv("MACHINE_ID")
	startDateStr := os.Getenv("START_DATE")

	var err error
	if startDateStr != "" {
		startDate, err = time.Parse(timeLayout, startDateStr)
	}

	if err != nil || startDateStr == "" {
		startDate = time.Date(2025, time.January, 1, 0, 0, 0, 0, time.UTC)
	}

	if machineIdStr == "" {
		panic("FATAL: Environment variable MACHINE_ID is not set.")
	}
	
	id64, err := strconv.ParseUint(machineIdStr, 10, 16)
	if err != nil {
		panic(fmt.Sprintf("FATAL: Invalid MACHINE_ID '%s': %v", machineIdStr, err))
	}
	machineId = uint16(id64)
	
	initSnowFlake()
}
