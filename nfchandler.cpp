#include "nfchandler.h"
#include "amiitool.h"
#include "amiibo.h"

#include <stdio.h>
#include <stdlib.h>
#include <nfc/nfc.h>

#define PAGE_COUNT 135
#define WRITE_COMMAND 0xA2
#define READ_COMMAND 0x30
#define AUTH_COMMAND 0x1B

const uint8_t dynamicLockBytes[4] = { 0x01, 0x00, 0x0f, 0xbd };
const uint8_t staticLockBytes[4] = { 0x00, 0x00, 0x0F, 0xE0 };

const nfc_modulation nmMifare = {
  .nmt = NMT_ISO14443A,
  .nbr = NBR_106,
};

NFCHandler::NFCHandler() {
  printf("Initializing NFC adapter\n");
  nfc_init(&context);

  if (!context) {
    printf("Unable to init libnfc (malloc)\n");
    exit(1);
  }

  device = nfc_open(context, NULL);

  if (device == NULL) {
    printf("ERROR: %s\n", "Unable to open NFC device.");
    exit(1);
  }

  if (nfc_initiator_init(device) < 0) {
    nfc_perror(device, "nfc_initiator_init");
    exit(1);
  }

  printf("NFC reader: opened\n");
}

void NFCHandler::readTagUUID(uint8_t uuidBuffer[]) {
  printf("***Scan tag***\n");

  if (nfc_initiator_select_passive_target(device, nmMifare, NULL, 0, &target) > 0) {
    printf("Read UID: ");
    int uidSize = target.nti.nai.szUidLen;
    Amiitool::shared()->printHex(target.nti.nai.abtUid, uidSize);

    if (UUID_SIZE != uidSize) {
      fprintf(stderr, "Read wrong size UID\n");
      exit(1);
    }

    for (int i = 0; i < UUID_SIZE; i++) {
      uuidBuffer[i] = target.nti.nai.abtUid[i];
    }
  }
}

void NFCHandler::writeAmiibo(Amiibo *amiibo) {
    uint8_t uuid[UUID_SIZE];
    readTagUUID(uuid);

    amiibo->setUUID(uuid);
    unlockAmiibo(amiibo);
    writeBuffer(amiibo->encryptedBuffer);
}

void NFCHandler::unlockAmiibo(const Amiibo *amiibo) {
  //TODO: check if this is already an amiibo:
  //TODO: check if AUTH0 is 04 (default 0xff) (page 131, byte 3 or address 527)
  //TODO: check if ACCESS is 5F (default 0x00) (page 132, byte 0 or address 528)
  printf("Trying to unlock Amiibo with generated PWD\n");
  uint8_t sendData[5] = {
    AUTH_COMMAND,
    amiibo->encryptedBuffer[532],
    amiibo->encryptedBuffer[533],
    amiibo->encryptedBuffer[534],
    amiibo->encryptedBuffer[535],
  };
  uint8_t recvData[2] = {};

  // Use raw send/receive methods
  if (nfc_device_set_property_bool(device, NP_EASY_FRAMING, false) < 0) {
		fprintf(stderr,  "nfc_device_set_property_bool false\n");
		exit(1);
	}

  int responseCode = nfc_initiator_transceive_bytes(device, sendData, 5, recvData, 2, 0);
  if (responseCode == 0) {
    if (recvData[0] != 0x80 || recvData[1] != 0x80) {
      fprintf(stderr, "Tag rejeted PWD");
    }
    printf("Done\n");
  } else {
    printf("Failed\n");
    fprintf(stderr, "Failed to unlock\n");
    nfc_perror(device, "Unlock");
    exit(1);
  }

  // Switch off raw send/receive methods
  if (nfc_device_set_property_bool(device, NP_EASY_FRAMING, true) < 0) {
		fprintf(stderr,  "nfc_device_set_property_bool true\n");
		exit(1);
}
}

void NFCHandler::writeBuffer(const uint8_t *buffer) {
  printf("Writing tag:\n");
  writeDataPages(buffer);
  writeDynamicLockBytes();
  writeStaticLockBytes();
  printf("Finished writing tag\n");
}

void NFCHandler::writeDataPages(const uint8_t *buffer) {
  printf("Writing encrypted bin:\n");
  for (uint8_t i = 3; i < PAGE_COUNT; i++) {
    writePage(i, buffer + (i * 4));
  }
  printf("Done\n");
}

void NFCHandler::writeDynamicLockBytes() {
  printf("Writing dynamic lock bytes\n");
  writePage(130, dynamicLockBytes);
  printf("Done\n");
}

void NFCHandler::writeStaticLockBytes() {
  printf("Writing static lock bytes\n");
  writePage(2, staticLockBytes);
  printf("Done\n");
}

void NFCHandler::writePage(uint8_t page, const uint8_t *buffer) {
    printf("Writing to %d: %02x %02x %02x %02x...",
         page, buffer[0], buffer[1], buffer[2], buffer[3]);

  uint8_t sendData[6] = {
    WRITE_COMMAND, page, buffer[0], buffer[1], buffer[2], buffer[3]
  };

  int responseCode = nfc_initiator_transceive_bytes(device, sendData, 6, NULL, 0, 0);

  if (responseCode == 0) {
    printf("Done\n");
  } else {
    printf("Failed\n");
    fprintf(stderr, "Failed to write to tag\n");
    nfc_perror(device, "Write");
    exit(1);
  }
}
