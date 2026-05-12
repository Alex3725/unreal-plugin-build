// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// ============================================================
//  CATEGORIA LOG DEDICATA ALLE SESSIONI ONLINE
//  Filtrabile nel Output Log di Unreal: LogMenuSystemOnline
// ============================================================

/**
 * @brief Categoria log per tutto ciò che riguarda sessioni online,
 *        subsistema Steam, ricerca e join sessioni multiplayer.
 *
 * Filtro nel Output Log: scrivi "LogMenuSystemOnline" nella barra filtro.
 */
DECLARE_LOG_CATEGORY_EXTERN(LogMenuSystemOnline, Log, All);


// ============================================================
//  FDebugUtils
//  Classe di utilità puramente statica per debug visivo e testuale.
//
//  UTILIZZO:
//    FDebugUtils::Success(TEXT("[CREATE] Sessione creata!"));
//    FDebugUtils::Error  (TEXT("[JOIN]   Sessione piena."));
//    FDebugUtils::Warning(FString::Printf(TEXT("Ping: %d ms"), Ping));
//    FDebugUtils::Section(TEXT("ONLINE SESSION INIT"));
//    FDebugUtils::Status (TEXT("⚡ JOINING SERVER... ATTENDI"));  // <-- persistente
//
//  NOTE:
//    - Messaggi a schermo disabilitati in UE_BUILD_SHIPPING.
//    - UE_LOG sempre attivo (utile per crash report in produzione).
//    - Status() usa un key fisso: sostituisce il messaggio precedente
//      invece di accumularne di nuovi. Ideale per stati di caricamento.
// ============================================================

/**
 * @brief Classe di utilità per debug visivo in UE5.
 *        Combina UE_LOG (log persistente) + AddOnScreenDebugMessage (schermo).
 *        Non istanziare: usa solo i metodi statici.
 */
class MENUSYSTEM_API FDebugUtils
{
public:

	// Non istanziabile
	FDebugUtils()                              = delete;
	~FDebugUtils()                             = delete;
	FDebugUtils(const FDebugUtils&)            = delete;
	FDebugUtils& operator=(const FDebugUtils&) = delete;

	// =======================================================
	// METODI PUBBLICI
	// =======================================================

	/**
	 * @brief Messaggio generico con colore e durata personalizzabili.
	 *
	 * @param Msg      Testo del messaggio.
	 * @param Color    Colore del testo a schermo (default: bianco).
	 * @param Duration Durata in secondi (default: 5s).
	 */
	static void Log(
		const FString& Msg,
		FColor         Color    = FColor::White,
		float          Duration = 5.f
	);

	/**
	 * @brief Messaggio informativo neutro (ciano).
	 *        Per stati, valori correnti, inizializzazioni.
	 *
	 * @param Msg      Testo del messaggio.
	 * @param Duration Durata in secondi (default: 5s).
	 */
	static void Info(
		const FString& Msg,
		float          Duration = 5.f
	);

	/**
	 * @brief Messaggio di successo (verde).
	 *        Per operazioni completate correttamente.
	 *
	 * @param Msg      Testo del messaggio.
	 * @param Duration Durata in secondi (default: 5s).
	 */
	static void Success(
		const FString& Msg,
		float          Duration = 5.f
	);

	/**
	 * @brief Messaggio di avvertimento (giallo).
	 *        Per situazioni anomale ma recuperabili.
	 *
	 * @param Msg      Testo del messaggio.
	 * @param Duration Durata in secondi (default: 8s).
	 */
	static void Warning(
		const FString& Msg,
		float          Duration = 8.f
	);

	/**
	 * @brief Messaggio di errore critico (rosso).
	 *        Per errori che bloccano il flusso normale.
	 *
	 * @param Msg      Testo del messaggio.
	 * @param Duration Durata in secondi (default: 10s).
	 */
	static void Error(
		const FString& Msg,
		float          Duration = 10.f
	);

	/**
	 * @brief Messaggio di stato PERSISTENTE (arancione, key fisso).
	 *
	 * Differenza chiave rispetto agli altri metodi:
	 *   - Usa un Key fisso (default: 200) invece di -1
	 *   - Ogni chiamata SOSTITUISCE il messaggio precedente con lo stesso Key
	 *   - Ideale per stati di caricamento che si aggiornano:
	 *       Status("🔍 CERCANDO...")  → Status("✅ TROVATO!")  → Status("⚡ JOINING...")
	 *   - Il vecchio messaggio scompare automaticamente quando ne arriva uno nuovo
	 *
	 * @param Msg      Testo del messaggio di stato.
	 * @param Key      Key dello slot a schermo (default: 200). Cambia per messaggi indipendenti.
	 * @param Duration Durata in secondi (default: 20s, più lungo per essere visibile).
	 */
	static void Status(
		const FString& Msg,
		int32          Key      = 200,
		float          Duration = 20.f
	);

	/**
	 * @brief Cancella il messaggio di stato dalla schermata.
	 *        Chiama questo metodo quando il flusso è terminato
	 *        (join completato, errore, timeout).
	 *
	 * @param Key Key del messaggio da cancellare (default: 200).
	 */
	static void ClearStatus(int32 Key = 200);

	/**
	 * @brief Separatore visivo con titolo di sezione.
	 *        Es: ===== [ CREATE SESSION ] =====
	 *
	 * @param Title Titolo della sezione.
	 */
	static void Section(const FString& Title);

	/**
	 * @brief Linea separatrice orizzontale senza titolo.
	 *        Es: --------------------------------------------------
	 */
	static void Separator();

private:

	/**
	 * @brief Implementazione interna condivisa da tutti i metodi pubblici.
	 *        Scrive su UE_LOG e AddOnScreenDebugMessage.
	 *
	 * @param Msg       Testo del messaggio.
	 * @param Color     Colore del messaggio a schermo.
	 * @param Duration  Durata in secondi del messaggio a schermo.
	 * @param Verbosity Verbosity di UE_LOG (Log, Warning, Error).
	 * @param Key       Key dello slot (-1 = sempre nuovo, >=0 = sovrascrive quello slot).
	 */
	static void PrintImpl(
		const FString&      Msg,
		FColor              Color,
		float               Duration,
		ELogVerbosity::Type Verbosity,
		int32               Key = -1
	);
};