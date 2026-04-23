# papers repo — 메타진화 엔진 + CLI 브레인스토밍 (2026-04-23)

요청자: user session.
범위: `/Users/ghost/core/papers/` 에 sister repos (hexa-lang / anima / nexus / airgenome / n6-architecture) 가 가진 수준의 meta engine + distribution CLI 를 주입. 고갈시까지 후보 나열.

관련 선행:
- `docs/meta_engine_cross_repo_analysis_20260423.md` — 5-repo 비교
- `docs/unified_meta_roadmap_cross_repo_20260423.md` — 12-repo 통합

---

## 0. 현황 인지

| 지표 | papers 값 | 비교 (hexa-lang) |
|---|---|---|
| `tool/*.hexa` 개수 | **0** | 251 |
| `bin/*` 개수 | 0 | 50+ |
| manifest.json papers | 120 | — |
| repo 파일 총계 | 5,778 | ~10,000 |
| Markdown 논문 | 239 | — |
| 기존 업로드 스크립트 | `upload_*.hexa/.sh` 4세트 ad-hoc | — |
| 메타엔진 | **없음** | 16 scanner real-v1 |
| 상호 inbox | 있음 (state/proposals/inventory.json) | 있음 |

**격차 크기**: papers 는 anima/hexa-lang 가 2달 전 상태. 초기 16 scanner + 배포 CLI 주입만 해도 생태계 균형 회복.

---

## 1. Distribution CLI 후보 (A 축 — 최우선, 사용자 요청 직접)

### A1. `papers publish` — 통합 배포 명령

```
bin/papers publish <paper-id>
  [--target zenodo|arxiv|osf|zenodo-dry|all]
  [--draft|--final]
  [--version patch|minor|major]
  [--dry-run]
```

- paper-id = manifest.json 의 id (예: `pa-n6-fusion-2026`)
- target 별 API/OAuth 토큰은 `~/.papers/credentials.toml` 에서 읽음
- dry-run: 메타데이터 검증 + 업로드 준비 PASS 여부만 출력
- 성공 시 manifest.json 의 `status`, `doi`, `published_ts` 자동 업데이트

| # | 서브기능 | 값 |
|---|---|---|
| A1a | Zenodo REST API | 현재 `upload_zenodo.hexa` 수동 스크립트 통합 |
| A1b | arXiv submission prep | PDF + metadata tarball 자동 빌드 |
| A1c | OSF integration | OSF pre-registration/preprint 경로 |
| A1d | GitHub release 동기화 | tag + release note 자동 생성 |
| A1e | 사본 verify | SHA-256 대조 (upload 후 download 되돌려 확인) |

### A2. `papers pull <doi>` — 역방향 회수

- 외부 DOI 로부터 metadata + PDF 를 manifest 에 흡수
- arXiv/Zenodo/CrossRef API 통합
- duplicate detection (title hash + abstract LSH)

### A3. `papers list` — manifest 브라우저

- 필터: `--status {draft|published|archived}`, `--project`, `--tier`, `--year`, `--keyword`
- 출력: box-draw 테이블 (roi-summary 양식과 동일)
- JSON 출력: `--json` 플래그

### A4. `papers new <template>` — 논문 골격 생성

- template = {paper-md, latex-standalone, jupyter, prereg, dataset-descriptor}
- manifest.json 에 draft entry + 디렉토리 skeleton
- frontmatter 자동 채움 (author, repo, tier, keywords placeholder)

### A5. `papers validate <paper-id>` — 사전 출판 lint

- 필수 필드: title, abstract, authors, keywords, license, references
- BibTeX 검증
- Figure 경로 존재 확인
- DOI 형식 검증
- abstract 길이 (Zenodo 5000자 제한 등)

### A6. `papers doi [--reserve]` — DOI 예약/조회

- Zenodo `prereserve_doi` API 호출
- 예약된 DOI 로 preprint 수정 (self-reference 가능)
- 전역 reserved-doi registry 유지

### A7. `papers cite <paper-id>` — 인용 포맷 빌더

- 출력 포맷: BibTeX / APA / MLA / IEEE / CFF / RIS
- clipboard copy 옵션 (`--copy`)
- `badge` 출력: `![DOI](shields.io/...)`

### A8. `papers mirror` — 다중 저장소 미러

- Zenodo + OSF + institutional repo 동시 업로드
- SHA 대조 + 불일치 시 자동 re-upload
- cross-reference: 각 버전이 서로를 Related identifier 로 링크

### A9. `papers release-notes <paper-id>` — release note 자동 작성

- manifest diff (prev version vs current) → CHANGELOG 섹션 생성
- peer-review threads, reviewer comments 정리

### A10. `papers submit-pr <target-journal>` — journal 제출 자동화

- target = NC|PRL|TVLSI|NeurIPS|JAIR|ICML|arXiv-cross-list
- 저널별 LaTeX template 교체 + reformat
- cover letter 템플릿 (manifest.json 에서 fills)
- pre-submission checklist lint

---

## 2. Meta-evolution Scanner 후보 (B 축 — hexa-lang hx_* 미러)

### B1-B16 scanner — sister 패턴 복제

| # | scanner | 목적 |
|---|---|---|
| B1 | `pp_missing_doi_scan` | manifest 에 DOI 없는 published 논문 |
| B2 | `pp_orphan_paper_scan` | manifest 에는 있는데 파일 없음 (또는 역) |
| B3 | `pp_citation_dead_link` | 내부/외부 레퍼런스 404/dead |
| B4 | `pp_figure_missing` | `!\[\]()` 참조하는데 실제 파일 없음 |
| B5 | `pp_bibkey_dup` | BibTeX key 중복 |
| B6 | `pp_abstract_length_drift` | Zenodo/arXiv 제한 초과 |
| B7 | `pp_tier_classification_gap` | tier 필드 누락 or inconsistent |
| B8 | `pp_keyword_cloud_drift` | keyword_index.json 와 manifest keyword 불일치 |
| B9 | `pp_license_mismatch` | 파일 내 라이선스 선언 vs manifest |
| B10 | `pp_readme_toc_drift` | README 목차 vs 실제 폴더 |
| B11 | `pp_cross_repo_refs` | n6 papers 가 anima 파일 가리키는지 |
| B12 | `pp_authorship_drift` | author 이름 표기 불일치 (Bob vs B. ...) |
| B13 | `pp_replication_stubs` | "TODO experiment", "results pending" 패턴 |
| B14 | `pp_checksum_drift` | 업로드된 Zenodo SHA256 vs local |
| B15 | `pp_outbound_link_rot` | URL liveness 로컬 스냅샷 |
| B16 | `pp_version_bloat` | v2/v3 동일 내용 (diff < 2%) |
| B17 | `pp_metadata_completeness` | ORCID / funder / keywords Fill rate |
| B18 | `pp_embargo_tracker` | embargo 해제일 지난 항목 |
| B19 | `pp_duplicate_title` | title 유사도 > 0.9 (LSH) |
| B20 | `pp_chain_break` | `cites`/`cited-by` 그래프 끊김 |

### B21-B30 meta engine orchestrator

| # | 항목 | 역할 |
|---|---|---|
| B21 | `pp_continuous_scan` | 20 scanner 오케스트레이터 (hexa-lang `hx_continuous_scan` 대응) |
| B22 | `pp_meta_telemetry` | 각 scan run 의 runtime/hits → JSONL |
| B23 | `pp_meta_gap_proposer` | scanner 출력 패턴 → 새 tool 제안 |
| B24 | `pp_declare_scanner` | 새 scanner DSL 로더 (hexa-lang 복제) |
| B25 | `pp_scanner_health` | scanner 자신의 drift (false positive rate 등) |
| B26 | `pp_stdlib_promote` | 중복 패턴 → `stdlib/papers/` primitive 승격 |
| B27 | `pp_cert_chain` | 논문 PDF 해시 → Merkle chain (anima cert_gate 응용) |
| B28 | `pp_bench_drift` | 논문 내 experiment 벤치 수치 history drift |
| B29 | `pp_api_drift` | Zenodo/arXiv API spec vs 우리 wrapper |
| B30 | `pp_raw_coverage` | raw# 규칙 적용률 (hexa-lang raw#15 mirror) |

---

## 3. Reader Experience / Browser (C 축)

### C1-C15

| # | 항목 |
|---|---|
| C1 | `papers serve` — local Jekyll/Hugo 서버 (현 docs/ 확장) |
| C2 | `papers search "<query>"` — full-text (keyword_index.json + abstract) |
| C3 | full-text search daemon — BM25 또는 tiny embedding (hexa native) |
| C4 | citation graph viz — graphviz dot 출력 |
| C5 | reading-list export — markdown / OPML / Zotero RDF |
| C6 | per-project dashboard (anima/nexus/n6 뷰) |
| C7 | "related paper" 추천 — keyword 교집합 |
| C8 | print-friendly CSS (docs/_site 전환) |
| C9 | mobile-first responsive |
| C10 | dark-mode toggle |
| C11 | LaTeX-to-HTML 변환 파이프라인 (pandoc wrapper) |
| C12 | JSON-LD / scholar schema.org 메타데이터 주입 |
| C13 | OpenGraph preview 최적화 (Twitter/GitHub share) |
| C14 | RSS/Atom feed 자동 생성 (새 논문 출판 시) |
| C15 | "What's new" auto changelog on homepage |

---

## 4. Citation / Provenance (D 축)

| # | 항목 |
|---|---|
| D1 | `papers lineage <paper-id>` — 선행/후속 DOI 그래프 |
| D2 | `papers trace-concept "<term>"` — 개념 최초 등장 논문 |
| D3 | cross-ref auto-build — 모든 `[N6-XXX]` 링크를 DOI 로 변환 |
| D4 | Zotero API 연동 — 라이브러리 양방향 |
| D5 | ORCID 수집 대시보드 — author 별 기여도 |
| D6 | co-author graph (Bacon number 스타일) |
| D7 | citation metrics 수집 (OpenCitations, COCI API) |
| D8 | h-index / i10-index 자체 계산 |
| D9 | "cited by Nature" 등 임팩트 스팟 |
| D10 | 표절 탐지 (유사도 > threshold) |

---

## 5. Cross-repo Bridges (E 축)

| # | 항목 |
|---|---|
| E1 | anima → papers auto-export: 매 release 시 새 PA-##.md |
| E2 | n6-architecture 논문 → papers/n6-architecture/ 자동 동기 |
| E3 | nexus discovery → papers fact claim 자동 드래프트 |
| E4 | papers → proposal_inbox: 리뷰어 comment 를 해당 repo 제안으로 |
| E5 | paper의 figure code → repo 로 역전개 (reproducibility) |
| E6 | GitHub release note ↔ Zenodo version 동기 |
| E7 | preprint server ping: arXiv 월간 카테고리 alert |
| E8 | hexa-lang → papers: language feature spec 업데이트 시 해당 논문 refresh |
| E9 | manifest.json 를 전 repo 의 inbox advisory 로 broadcast |

---

## 6. Quality / Lint (F 축)

| # | 항목 |
|---|---|
| F1 | frontmatter schema validator (YAML-LD) |
| F2 | BibTeX entropy — 중복 key / 빈 필드 |
| F3 | figure DPI check — low-res 이미지 경고 |
| F4 | alt-text 필수 — 접근성 |
| F5 | heading hierarchy (H1→H2→H3 순서) |
| F6 | spell check (en + ko) — 전문 용어 whitelist |
| F7 | passive-voice ratio 경고 |
| F8 | abstract vs body keyword 일치 |
| F9 | broken `[[wikilink]]` |
| F10 | mermaid diagram syntax check |
| F11 | TeX $...$ 렌더 검증 |
| F12 | table column count 일관성 |

---

## 7. Reproducibility / Compliance (G 축)

| # | 항목 |
|---|---|
| G1 | `papers repro <paper-id>` — 선언된 repo commit pin 재실행 |
| G2 | dataset DOI attachment — Zenodo dataset record 자동 링크 |
| G3 | FAIR score dashboard — Findable/Accessible/Interoperable/Reusable |
| G4 | GDPR/privacy 자동 scan (이름 IP 이메일 pattern) |
| G5 | funding ack 필수 field |
| G6 | competing-interest 선언 |
| G7 | pre-registration link 요구 |
| G8 | code availability 선언 (license + git tag) |
| G9 | data availability 선언 (Zenodo/OSF/institutional) |
| G10 | ethics approval chain (IRB 없으면 exempt 명시) |

---

## 8. 논문 배포 브레인스토밍 (H 축 — user 강조 부분)

### H1-H20 배포 채널

| # | 채널 | 자동화 수준 |
|---|---|---|
| H1 | Zenodo (DOI) | **core — A1a 로 커버** |
| H2 | arXiv | moderate — A1b |
| H3 | OSF | moderate — A1c |
| H4 | ResearchGate | manual (API 제한) |
| H5 | SSRN | manual |
| H6 | bioRxiv/medRxiv | moderate |
| H7 | HAL (프랑스 국립) | low priority |
| H8 | Semantic Scholar | API indexing only |
| H9 | ORCID Works | auto once ORCID token 있으면 |
| H10 | Google Scholar | crawler 기반 (명시 업로드 X) |
| H11 | Twitter/X 자동 공지 | API + 이미지 카드 자동 생성 |
| H12 | Mastodon/Bluesky | 동일 패턴 |
| H13 | GitHub Discussions 자동 공지 | repo discussions API |
| H14 | Mailing list auto-blast | `contact/` repo 연동 |
| H15 | arXiv-cross-list 자동 반영 | 연계 카테고리 추천 |
| H16 | EuropePMC | 생의학 한정 |
| H17 | DBLP | 컴공 한정, crawler |
| H18 | Crossref deposit | publisher 역할 시 |
| H19 | LinkedIn 게시 (manual copy button) | 이미지 + caption 자동 생성 |
| H20 | Newsletter digest (주간) | contact/ 연동 |

### H21-H30 배포 전후 공정

| # | 항목 |
|---|---|
| H21 | pre-flight checklist runner (papers validate 확장) |
| H22 | embargo scheduler (날짜 되면 자동 publish) |
| H23 | staged rollout: draft → preprint → final |
| H24 | post-publish stats collector (downloads, views, alt-metrics) |
| H25 | DOI mint monitoring (CrossRef status poll) |
| H26 | retraction workflow (찰칵 retraction DOI) |
| H27 | errata/corrigendum CLI (`papers erratum <id>`) |
| H28 | version bump UI (semver 규약) |
| H29 | mirror verification (다른 저장소 sha 대조) |
| H30 | citation receipt (타인이 인용 시 알림) |

---

## 9. 검색 / 색인 (I 축)

| # | 항목 |
|---|---|
| I1 | keyword_index.json 자동 재빌드 |
| I2 | vector embedding (local, hexa native) |
| I3 | 약어 dict 자동 추출 |
| I4 | abstract TL;DR 1-line 자동 (길이 기반 발췌, 오프라인) |
| I5 | 주요 수식 hash (LaTeX AST) |
| I6 | figure captions 별도 색인 |
| I7 | references-only index (citation 네트워크 탐색) |
| I8 | topic cluster (UMAP 2D 시각) |
| I9 | similar-paper finder |
| I10 | time-series trend (연도별 키워드 빈도) |

---

## 10. 협업 / PR flow (J 축)

| # | 항목 |
|---|---|
| J1 | `papers review <paper-id>` — 단일 rounds 리뷰 comment aggregator |
| J2 | GitHub PR 템플릿 (논문 submission form) |
| J3 | reviewer assignment 로테이션 |
| J4 | 리뷰어 익명화 도구 (double-blind 시뮬) |
| J5 | response-to-reviewer 템플릿 빌더 |
| J6 | rebuttal diff (원고 vs 수정본 changebars) |
| J7 | track-changes export (Word-like) |
| J8 | 공저자 승인 서명 flow (cert chain 응용) |

---

## 11. Analytics / Metrics (K 축)

| # | 항목 |
|---|---|
| K1 | Zenodo stats daily poll → `state/stats.jsonl` |
| K2 | 다운로드 geo map |
| K3 | download spike alert |
| K4 | altmetric 점수 tracker |
| K5 | 인용 지연 분포 |
| K6 | reading-time prediction |
| K7 | funnel: preprint → peer-review → published |
| K8 | cost per paper (시간/자원) |
| K9 | repo health score (update recency, PR count, citations) |
| K10 | peer-review turnaround time |

---

## 12. Figure / Asset Pipeline (L 축)

| # | 항목 |
|---|---|
| L1 | figure source (py/ipynb/mermaid/tikz) → SVG/PNG 자동 빌드 |
| L2 | figure cache invalidation (source change mtime) |
| L3 | colorblind palette 적용 lint |
| L4 | figure licensing (CC-BY 등) 메타데이터 임베딩 |
| L5 | Zenodo figure-only deposit (supplementary) |
| L6 | 고해상도 PDF embed |
| L7 | video supplement upload helper |
| L8 | 3D model viewer (GLB/USDZ) integration |
| L9 | interactive figure (Observable/Plotly) export |
| L10 | figure diff viewer (version 비교) |

---

## 13. Preservation / Archive (M 축)

| # | 항목 |
|---|---|
| M1 | Internet Archive SavePageNow 자동 |
| M2 | web.archive.org snapshot verify |
| M3 | LOCKSS / CLOCKSS 제출 |
| M4 | Software Heritage archive ping |
| M5 | torrent seed (IPFS 또는 academic torrent) |
| M6 | link-rot sentinel — 외부 링크 주간 체크 |
| M7 | local full-text snapshot (external refs) |
| M8 | OAI-PMH endpoint (리포지토리 노출) |
| M9 | long-term preservation policy 선언 |
| M10 | succession plan (maintainer 부재 시) |

---

## 14. Templates / Scaffolding (N 축)

| # | 항목 |
|---|---|
| N1 | paper template pack (5종 양식) |
| N2 | journal별 LaTeX .cls 관리 |
| N3 | bibliography starter (domain별) |
| N4 | figure starter (matplotlib/mermaid/tikz) |
| N5 | PRE-registration template (OSF 호환) |
| N6 | data-descriptor template (Scientific Data 스타일) |
| N7 | survey-paper template |
| N8 | case-study template |
| N9 | replication-report template |
| N10 | opinion/perspective 템플릿 |

---

## 15. AI Integration (O 축 — 주의 필요)

| # | 항목 | 주의 |
|---|---|---|
| O1 | Abstract 자동 translation (en↔ko) | quality check 필수 |
| O2 | 용어 정규화 제안 | manual confirm |
| O3 | citation 누락 후보 감지 | false positive 높음 |
| O4 | plagiarism-self 탐지 (이전 논문 대비 overlap) | |
| O5 | figure alt-text 자동 생성 | |
| O6 | 리뷰어 톤 분석 (긍정/부정/건설적) | |
| O7 | abstract ↔ body 일관성 점수 | |
| O8 | follow-up question 제안 | |
| O9 | related work section 빈틈 감지 | |
| O10 | hypothesis grammar linter | |

---

## 16. i18n / Translation (P 축)

| # | 항목 |
|---|---|
| P1 | multi-lingual abstract (ko/en/ja/zh) slot |
| P2 | 제목 alt-language 메타데이터 |
| P3 | reference works in native script |
| P4 | unicode NFC 정규화 |
| P5 | BibTeX Unicode safe (biblatex) |
| P6 | right-to-left (ar/he) 레이아웃 |
| P7 | 한자 키워드 색인 |
| P8 | 전문용어 dict 교환 (대학별) |

---

## 17. Alerts / Hooks (Q 축)

| # | 항목 |
|---|---|
| Q1 | 신규 인용 감지 시 anima/nexus inbox 로 advisory |
| Q2 | Zenodo API 정책 변경 시 watcher |
| Q3 | DOI resolve 실패 알림 (shields.io 403 같은 사태 자동 감지) |
| Q4 | embargo release countdown |
| Q5 | peer-review 30일 stalled 경고 |
| Q6 | figure source 오래됨 경고 |
| Q7 | 라이선스 호환성 경고 (GPL + CC-BY-ND 충돌 등) |
| Q8 | pre-print 180일 후에도 저널 미투고 경고 |

---

## 18. Testing (R 축)

| # | 항목 |
|---|---|
| R1 | `stdlib/papers/test/` — schema / validator 테스트 |
| R2 | upload dry-run end-to-end (sandbox Zenodo) |
| R3 | manifest round-trip (parse → emit → 동일) |
| R4 | keyword_index regen deterministic |
| R5 | figure build reproducibility (hash stable) |
| R6 | citation resolve offline fixtures |
| R7 | template scaffold smoke |
| R8 | BibTeX parser fuzz |
| R9 | LaTeX→HTML golden files |
| R10 | CLI help text snapshot (regression) |

---

## 19. 기타 특수 (S 축)

| # | 항목 |
|---|---|
| S1 | pre-commit hook — staged 논문 validate |
| S2 | `CITATION.cff` 자동 생성 per-paper |
| S3 | 논문별 Changelog.md |
| S4 | author 이름 변경/결혼 후 자동 alias |
| S5 | DEI/demographics tracker (optional) |
| S6 | conference submission 마감일 캘린더 |
| S7 | funding acknowledgment 자동 삽입 |
| S8 | conflict-of-interest matrix (author × funder) |
| S9 | supplementary material 전용 Zenodo deposit |
| S10 | talk / video 링크 관리 (YouTube / Vimeo) |
| S11 | poster PDF 자동 생성 |
| S12 | 논문 배경색/브랜딩 (프로젝트별 색) |
| S13 | "paper of the week" 자동 픽커 |
| S14 | 주간 digest email 자동 작성 |
| S15 | conference booth QR → paper bundle |

---

## 20. 메타 엔진 전용 infra (T 축)

| # | 항목 |
|---|---|
| T1 | `tool/pp_meta.hexa` — dispatcher (scanner run/list) |
| T2 | `bin/pp_meta` shim |
| T3 | hexa AOT `bin/papers` — interpreter 우회 |
| T4 | continuous_scan launchd agent (매일 01:00) |
| T5 | convergence log `state/convergence.jsonl` |
| T6 | proposal_inbox 확장 — kind 에 `paper_gap` 추가 |
| T7 | escalator 연동 (arXiv 업로드 시 Claude Code sandbox 우회) |
| T8 | drill 통합 — `drill papers <seed>` |
| T9 | roadmap_engine 합류 (.roadmap 파일 papers 에도 도입) |
| T10 | tool/hx_bare_hexa_scan 대응 pp 버전 |

---

## 21. 즉시 적용 TOP 15 (min-LOC + blocker 0)

| 순위 | id | 항목 | 이유 |
|---|---|---|---|
| 1 | T1+T2 | `pp_meta` dispatcher + shim | 모든 scanner 의 진입점 |
| 2 | A3 | `papers list` — manifest 브라우저 | 즉시 사용자 가치 |
| 3 | A5 | `papers validate` — 사전 출판 lint | 배포 실수 차단 |
| 4 | A1 | `papers publish` — Zenodo 업로드 (기존 스크립트 흡수) | 핵심 기능 |
| 5 | B1 | `pp_missing_doi_scan` | manifest clean-up |
| 6 | B2 | `pp_orphan_paper_scan` | manifest 무결성 |
| 7 | B21 | `pp_continuous_scan` | orchestrator |
| 8 | A7 | `papers cite` — 인용 포맷 | 사용자 편의 |
| 9 | E1 | anima → papers auto-export | cross-repo bridge |
| 10 | T6 | proposal_inbox `paper_gap` kind 확장 | feedback loop |
| 11 | F1 | frontmatter validator | quality 하한선 |
| 12 | S1 | pre-commit hook | regression 차단 |
| 13 | A2 | `papers pull <doi>` — 외부 흡수 | library 생산성 |
| 14 | C2 | `papers search` | 논문 120개에서 검색 |
| 15 | T4 | launchd daily continuous_scan | 자율 운용 |

---

## 22. 고갈점 — 이 이상 추가 가치 희박 (U 축)

다음은 위 20 카테고리에 이미 포함되지 않은 "변두리" 아이디어. 더 생각하려 해도 겹치기 시작:

- 논문 heatmap (년도 × 저자)
- emoji reaction 축약 통계
- 서점 형식 "내책 샘플 미리보기" 5-page 추출
- 특허/논문 교차 참조 (Google Patents API)
- 학회 별 reviewer rubric 저장
- 공개키 서명 필수화 (저자 신원 보증)
- 논문별 podcast / lay-summary 자동 생성 (오프라인 ASR 없음, skip)
- 드론 배송 (농담)

이상은 cost/benefit 낮아 제외.

---

## 23. 구현 순서 제안 (H-MINPATH)

### Phase 1 (1-2 세션, ~500 LOC)
- T1, T2 infrastructure
- A3 (list), A5 (validate) CLI
- B1, B2 두 scanner
- T6 inbox kind 확장

### Phase 2 (2-3 세션, ~1500 LOC)
- A1 publish (Zenodo API full)
- A7 cite
- B21 continuous_scan
- F1 frontmatter validator
- S1 pre-commit hook

### Phase 3 (지속)
- 잔여 17 scanner
- Cross-repo bridges (E1-E9)
- Reader experience (C 축)
- Analytics (K 축)

---

## 24. 질문/결정 필요

1. **credential 저장 위치**: `~/.papers/credentials.toml` vs `secret/papers.yaml` vs macOS Keychain
2. **Zenodo sandbox vs production**: dry-run 기본값?
3. **manifest.json 스키마 versioning**: v1 → v2 전환 시 migration tool?
4. **다국어 abstract**: 필수 vs optional
5. **repo branching**: paper 별 branch? 또는 main-only?
6. **PR 워크플로우**: 논문 수정은 PR 강제 vs direct push 허용?

위 선택 주시면 바로 Phase 1 구현 착수 가능합니다.

---

## 종합

총 나열: **245+ 항목** (A~U 21개 축). hexa-lang/anima 수준의 full meta engine + distribution CLI 까지 3 phase 로 도달 가능. Phase 1 만으로도 즉시 사용 가능한 최소 viable engine 완성 (500 LOC 규모, 15개 항목).
