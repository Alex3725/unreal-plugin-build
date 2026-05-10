// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// ============================================================
//  CATEGORIA LOG DEDICATA ALLE SESSIONI ONLINE
//  Separata da LogMenuSystem (generale) per poter filtrare
//  nel Output Log di Unreal solo i messaggi multiplayer:
//    Filtro Output Log --> LogMenuSystemOnline
// ============================================================

/**
 * @brief Categoria log dedicata a tutto ciò che riguarda le sessioni online,
 *        il subsistema Steam, la ricerca e il join delle sessioni multiplayer.
 *
 * Utilizzo:
 *   UE_LOG(LogMenuSystemOnline, Log,     TEXT("messaggio normale"));
 *   UE_LOG(LogMenuSystemOnline, Warning, TEXT("attenzione"));
 *   UE_LOG(LogMenuSystemOnline, Error,   TEXT("errore critico"));
 *
 * Filtro nel Output Log di Unreal Editor:
 *   scrivi "LogMenuSystemOnline" nella barra filtro per vedere solo questi log.
 */
DECLARE_LOG_CATEGORY_EXTERN(LogMenuSystemOnline, Log, All);


// ============================================================
//  FDebugUtils
//  Classe di utilità pura (nessun UObject, nessuna istanza).
//  Tutti i metodi sono statici e possono essere chiamati
//  da qualunque punto del codice senza includere dipendenze pesanti.
//
//  PATTERN DI UTILIZZO:
//    FDebugUtils::Success(TEXT("[CREATE] Sessione creata!"));
//    FDebugUtils::Error  (TEXT("[JOIN]   Sessione piena."));
//    FDebugUtils::Warning(FString::Printf(TEXT("Ping: %d ms"), Ping));
//    FDebugUtils::Section(TEXT("ONLINE SESSION INIT"));
//
//  NOTE:
//    - I messaggi a schermo (AddOnScreenDebugMessage) sono disabilitati
//      automaticamente in UE_BUILD_SHIPPING per evitare spam in produzione.
//    - I log UE_LOG rimangono sempre attivi (utili per crash report in shipping).
//    - La durata dei messaggi a schermo è configurabile per ogni metodo.
// ============================================================

/**
 * @brief Classe di utilità per il debug visivo e testuale in Unreal Engine 5.
 *
 * Fornisce metodi statici che combinano:
 *   1. UE_LOG  (log persistente su file e Output Log)
 *   2. AddOnScreenDebugMessage (messaggio colorato su schermo durante il gioco)
 *
 * Tutte le funzioni usano la categoria @ref LogMenuSystemOnline.
 * I messaggi a schermo sono compilati solo in configurazioni non-Shipping.
 *
 * Non istanziare questa classe: usa solo i metodi statici.
 */
class MENUSYSTEM_API FDebugUtils
{
public:

	// -------------------------------------------------------
	// DISABILITA costruttore/copia/move:
	// questa classe è puramente statica, non va mai istanziata.
	// -------------------------------------------------------
	FDebugUtils()  = delete;
	~FDebugUtils() = delete;
	FDebugUtils(const FDebugUtils&)            = delete;
	FDebugUtils& operator=(const FDebugUtils&) = delete;

	// =======================================================
	// METODI PUBBLICI
	// =======================================================

	/**
	 * @brief Messaggio generico con colore e durata personalizzabili.
	 *        Combina UE_LOG(Log) + AddOnScreenDebugMessage.
	 *
	 * @param Msg      Testo del messaggio da visualizzare.
	 * @param Color    Colore del testo a schermo (default: bianco).
	 * @param Duration Durata in secondi del messaggio a schermo (default: 5s).
	 */
	static void Log(
		const FString& Msg,
		FColor         Color    = FColor::White,
		float          Duration = 5.f
	);

	/**
	 * @brief Messaggio informativo (bianco/ciano).
	 *        Usa UE_LOG(Log) + schermo bianco.
	 *        Ideale per informazioni di stato neutre.
	 *
	 * @param Msg      Testo del messaggio.
	 * @param Duration Durata in secondi del messaggio a schermo (default: 5s).
	 */
	static void Info(
		const FString& Msg,
		float          Duration = 5.f
	);

	/**
	 * @brief Messaggio di successo (verde).
	 *        Usa UE_LOG(Log) + schermo verde.
	 *        Ideale per confermare operazioni completate con successo.
	 *
	 * @param Msg      Testo del messaggio.
	 * @param Duration Durata in secondi del messaggio a schermo (default: 5s).
	 */
	static void Success(
		const FString& Msg,
		float          Duration = 5.f
	);

	/**
	 * @brief Messaggio di avvertimento (giallo).
	 *        Usa UE_LOG(Warning) + schermo giallo.
	 *        Ideale per situazioni anomale ma non fatali.
	 *
	 * @param Msg      Testo del messaggio.
	 * @param Duration Durata in secondi del messaggio a schermo (default: 8s).
	 */
	static void Warning(
		const FString& Msg,
		float          Duration = 8.f
	);

	/**
	 * @brief Messaggio di errore (rosso).
	 *        Usa UE_LOG(Error) + schermo rosso.
	 *        Ideale per errori critici che impediscono il corretto funzionamento.
	 *
	 * @param Msg      Testo del messaggio.
	 * @param Duration Durata in secondi del messaggio a schermo (default: 10s).
	 */
	static void Error(
		const FString& Msg,
		float          Duration = 10.f
	);

	/**
	 * @brief Stampa un separatore visivo con un titolo di sezione.
	 *        Utile per separare visivamente gruppi di log correlati.
	 *        Esempio output:
	 *          ===== [ CREATE SESSION ] =====
	 *
	 * @param Title Titolo della sezione (es. "CREATE SESSION", "FIND SESSIONS").
	 */
	static void Section(const FString& Title);

	/**
	 * @brief Stampa una linea separatrice orizzontale senza titolo.
	 *        Utile per separare visivamente blocchi di log nel Output Log.
	 *        Esempio output:
	 *          --------------------------------------------------
	 */
	static void Separator();

private:

	/**
	 * @brief Implementazione interna condivisa da tutti i metodi pubblici.
	 *        Gestisce sia UE_LOG che AddOnScreenDebugMessage.
	 *
	 * @param Msg       Testo del messaggio.
	 * @param Color     Colore del messaggio a schermo.
	 * @param Duration  Durata in secondi del messaggio a schermo.
	 * @param Verbosity Verbosity di UE_LOG (Log, Warning, Error).
	 */
	static void PrintImpl(
		const FString&    Msg,
		FColor            Color,
		float             Duration,
		ELogVerbosity::Type Verbosity
	);
};