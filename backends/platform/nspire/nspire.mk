TARGET := scummvm

GENZEHN  ?= genzehn
MAKEPRG  ?= make-prg

.PHONY: clean_nspire dist_nspire

clean: clean_nspire

clean_nspire:
	$(RM) $(TARGET).tns
	$(RM) $(TARGET).tns.zehn
	$(RM) $(TARGET).elf
	$(RM) -rf dist_nspire

# Build the .tns file from the ELF executable
# NOTE: Do NOT strip the ELF - strip removes .rel.* sections needed by genzehn for relocations
$(TARGET).tns: $(EXECUTABLE)
	@echo "Creating TI-Nspire executable..."
	$(GENZEHN) --input $< --output $@.zehn --name "scummvm" --uses-lcd-blit true --240x320-support true --compress
	$(MAKEPRG) $@.zehn $@
	$(RM) $@.zehn
	@mkdir -p prod
	@cp $(TARGET).tns prod/
	@echo "Built $(TARGET).tns (copied to prod/)"

dist_nspire: $(TARGET).tns
	@rm -rf dist_nspire
	@mkdir -p dist_nspire
	@mkdir -p dist_nspire/scummvm/data
	@mkdir -p dist_nspire/scummvm/saves
	@cp $(TARGET).tns dist_nspire/
ifdef DIST_FILES_THEMES
	@cp $(DIST_FILES_THEMES) dist_nspire/scummvm/data/
endif
ifdef DIST_FILES_ENGINEDATA
	@cp $(DIST_FILES_ENGINEDATA) dist_nspire/scummvm/data/
endif
ifdef DIST_FILES_VKEYBD
	@cp $(DIST_FILES_VKEYBD) dist_nspire/scummvm/data/
endif
	@echo "Distribution created in dist_nspire/"
