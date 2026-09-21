const labels = Object.freeze({
  pl: {
    state: "Stan", output: "Wyjście", recoveries: "Odzyskania",
    retries: "Ponowienia", events: "Zdarzenia", bounded: "Bufory ograniczone",
    mailbox: "Skrzynka", rejected: "Odrzucone", epochs: "Epoki",
    clock: "Zegar", probes: "Pomiary sprzętu"
  },
  en: {
    state: "State", output: "Output", recoveries: "Recoveries",
    retries: "Retries", events: "Events", bounded: "Bounded buffers",
    mailbox: "Mailbox", rejected: "Rejected", epochs: "Epochs",
    clock: "Clock", probes: "Hardware probes"
  }
});

export function formatRuntimeDiagnostics(snapshot, locale = "pl") {
  const copy = labels[locale] ?? labels.pl;
  const retries = snapshot.counters.networkRetries + snapshot.counters.streamRetries + snapshot.counters.bluetoothRetries;
  return Object.freeze([
    `${copy.state}: ${snapshot.state}`,
    `${copy.output}: ${snapshot.output}`,
    `${copy.recoveries}: ${snapshot.counters.recoveries}`,
    `${copy.retries}: ${retries}`,
    `${copy.events}: ${snapshot.counters.processedEvents}`,
    `${copy.bounded}: Q${snapshot.queueDepth}/T${snapshot.timerCount}/D${snapshot.diagnosticsStored}`
  ]);
}

export function formatIngressDiagnostics(snapshot, locale = "pl") {
  const copy = labels[locale] ?? labels.pl;
  const counters = snapshot.counters;
  const rejected = counters.mailboxOverflows + counters.invalidFacts +
    counters.duplicateFacts + counters.staleFacts + counters.staleEpochs +
    counters.backwardTicks + counters.contentionDrops + counters.deliveryRejected;
  return Object.freeze([
    `${copy.mailbox}: ${snapshot.mailboxDepth}/${counters.maximumMailboxDepth}`,
    `${copy.rejected}: ${rejected}`,
    `${copy.epochs}: ${counters.staleEpochs}`,
    `${copy.clock}: ${snapshot.clockTick}/R${counters.rollovers}`,
    `${copy.probes}: ${snapshot.resourceProbes.internalHeapFreeBytes}`
  ]);
}
